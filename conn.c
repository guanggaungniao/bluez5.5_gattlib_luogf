#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <sys/socket.h>
#include "glib-2.0/glib.h"

#include "bluetooth/bluetooth.h"
#include "bluetooth/l2cap.h"
#include "bluetooth/hci.h"
#include "bluetooth/hci_lib.h"
#include "bluetooth/sdp.h"

#include "uuid.h"
#include "src/shared/mainloop.h"
#include "src/shared/util.h"
#include "src/shared/att.h"
#include "src/shared/queue.h"
#include "src/shared/gatt-db.h"
#include "src/shared/gatt-client.h"


#define COLOR_OFF	"\x1B[0m"
#define COLOR_RED	"\x1B[0;91m"
#define COLOR_GREEN	"\x1B[0;92m"
#define COLOR_YELLOW	"\x1B[0;93m"
#define COLOR_BLUE	"\x1B[0;94m"
#define COLOR_MAGENTA	"\x1B[0;95m"
#define COLOR_BOLDGRAY	"\x1B[1;30m"
#define COLOR_BOLDWHITE	"\x1B[1;37m"

#define ATT_CID 4

struct gattlib_thread_t {
	int           ref;
	pthread_t     thread;
	GMainContext* loop_context;
	GMainLoop*    loop;
};

struct client {
	int fd;
	struct bt_att *att;
	struct gatt_db *db;
	struct bt_gatt_client *gatt;

	unsigned int reliable_session_id;
};



//打印UUID
static void print_uuid(const bt_uuid_t *uuid)
{
	char uuid_str[MAX_LEN_UUID_STR];
	bt_uuid_t uuid128;

	bt_uuid_to_uuid128(uuid, &uuid128);
	bt_uuid_to_string(&uuid128, uuid_str, sizeof(uuid_str));

	printf("%s\n", uuid_str);
}

static void print_prompt(void)
{
	printf(COLOR_BLUE "[GATT client]" COLOR_OFF "# ");
	fflush(stdout);
}
static void print_incl(struct gatt_db_attribute *attr, void *user_data)
{
	struct client *cli = user_data;
	uint16_t handle, start, end;
	struct gatt_db_attribute *service;
	bt_uuid_t uuid;

	if (!gatt_db_attribute_get_incl_data(attr, &handle, &start, &end))
		return;

	service = gatt_db_get_attribute(cli->db, start);
	if (!service)
		return;

	gatt_db_attribute_get_service_uuid(service, &uuid);

	printf("\t  " COLOR_GREEN "include" COLOR_OFF " - handle: "
					"0x%04x, - start: 0x%04x, end: 0x%04x,"
					"uuid: ", handle, start, end);
	print_uuid(&uuid);
}
static void print_desc(struct gatt_db_attribute *attr, void *user_data)
{
	printf("\t\t  " COLOR_MAGENTA "descr" COLOR_OFF
					" - handle: 0x%04x, uuid: ",
					gatt_db_attribute_get_handle(attr));
	print_uuid(gatt_db_attribute_get_type(attr));
}
static void print_chrc(struct gatt_db_attribute *attr, void *user_data)
{
	uint16_t handle, value_handle;
	uint8_t properties;
	uint16_t ext_prop;
	bt_uuid_t uuid;

	if (!gatt_db_attribute_get_char_data(attr, &handle,
								&value_handle,
								&properties,
								&ext_prop,
								&uuid))
		return;

	printf("\t  " COLOR_YELLOW "charac" COLOR_OFF
				" - start: 0x%04x, value: 0x%04x, "
				"props: 0x%02x, ext_props: 0x%04x, uuid: ",
				handle, value_handle, properties, ext_prop);
	print_uuid(&uuid);

	gatt_db_service_foreach_desc(attr, print_desc, NULL);
}
//打印改变的service
static void print_service(struct gatt_db_attribute *attr, void *user_data)
{
	struct client *cli = user_data;
	uint16_t start, end;
	bool primary;
	bt_uuid_t uuid;

	if (!gatt_db_attribute_get_service_data(attr, &start, &end, &primary,
									&uuid))
		return;

	printf(COLOR_RED "service" COLOR_OFF " - start: 0x%04x, "
				"end: 0x%04x, type: %s, uuid: ",
				start, end, primary ? "primary" : "secondary");
	print_uuid(&uuid);

	gatt_db_service_foreach_incl(attr, print_incl, cli);
	gatt_db_service_foreach_char(attr, print_chrc, NULL);

	printf("\n");
}
//service change回调函数
static void service_changed_cb(uint16_t start_handle, uint16_t end_handle,
								void *user_data)
{
	struct client *cli = user_data;

	printf("\nService Changed handled - start: 0x%04x end: 0x%04x\n",
						start_handle, end_handle);

	gatt_db_foreach_service_in_range(cli->db, NULL, print_service, cli,
						start_handle, end_handle);
	print_prompt();
}
static void print_services(struct client *cli)
{
	printf("\n");

	gatt_db_foreach_service(cli->db, NULL, print_service, cli);
}
static void ready_cb(bool success, uint8_t att_ecode, void *user_data)
{
	struct client *cli = user_data;

	if (!success) {
		printf("GATT discovery procedures failed - error code: 0x%02x\n",att_ecode);
		return;
	}

	printf("GATT discovery procedures complete\n");

	print_services(cli);
	print_prompt();
}
//service删除回调
static void service_removed_cb(struct gatt_db_attribute *attr, void *user_data)
{
	char uuid_str[MAX_LEN_UUID_STR];
	bt_uuid_t uuid;
	uint16_t start, end;

	gatt_db_attribute_get_service_uuid(attr, &uuid);
	bt_uuid_to_string(&uuid, uuid_str, sizeof(uuid_str));

	gatt_db_attribute_get_service_handles(attr, &start, &end);

	printf("serviceremove - UUID: %s start: 0x%04x end: 0x%04x\n", uuid_str,start, end);
}
//打印当前service
static void service_added_cb(struct gatt_db_attribute *attr, void *user_data)
{
	char uuid_str[MAX_LEN_UUID_STR];
	bt_uuid_t uuid;
	uint16_t start, end;

	gatt_db_attribute_get_service_uuid(attr, &uuid);
	bt_uuid_to_string(&uuid, uuid_str, sizeof(uuid_str));

	gatt_db_attribute_get_service_handles(attr, &start, &end);
	printf("serviceadd - UUID: %s start: 0x%04x end: 0x%04x\n",  uuid_str,start, end);
}
//断开回调函数
static void att_disconnect_cb(int err, void *user_data)
{
	printf("Device disconnected: %s\n", strerror(err));
}
static void att_debug_cb(const char *str, void *user_data)
{
	const char *prefix = user_data;

	printf(COLOR_BOLDGRAY "%s" COLOR_BOLDWHITE "%s\n" COLOR_OFF, prefix, str);
}
static void gatt_debug_cb(const char *str, void *user_data)
{
	const char *prefix = user_data;

	printf(COLOR_GREEN "%s%s\n" COLOR_OFF, prefix, str);
}
static struct client *client_create(int fd, uint16_t mtu)
{
	struct client *cli;

	cli = new0(struct client, 1);
	if (!cli) {
		printf("Failed to allocate memory for client\n");
		return NULL;
	}


	cli->att = bt_att_new(fd, false);
	if (!cli->att) {
		printf(stderr, "Failed to initialze ATT transport layer\n");
		bt_att_unref(cli->att);
		free(cli);
		return NULL;
	}

	if (!bt_att_set_close_on_unref(cli->att, true)) {
		printf("Failed to set up ATT transport layer\n");
		bt_att_unref(cli->att);
		free(cli);
		return NULL;
	}

	if (!bt_att_register_disconnect(cli->att, att_disconnect_cb, NULL,
								NULL)) {
		printf(stderr, "Failed to set ATT disconnect handler\n");
		bt_att_unref(cli->att);
		free(cli);
		return NULL;
	}

	cli->fd = fd;
	cli->db = gatt_db_new();
	if (!cli->db) {
		printf("Failed to create GATT database\n");
		bt_att_unref(cli->att);
		free(cli);
		return NULL;
	}

	cli->gatt = bt_gatt_client_new(cli->db, cli->att, mtu);
	if (!cli->gatt) {
		printf("Failed to create GATT client\n");
		gatt_db_unref(cli->db);
		bt_att_unref(cli->att);
		free(cli);
		return NULL;
	}

	gatt_db_register(cli->db, service_added_cb, service_removed_cb,NULL, NULL);

	bt_att_set_debug(cli->att, att_debug_cb, "att: ", NULL);
		bt_gatt_client_set_debug(cli->gatt, gatt_debug_cb, "gatt: ",
									NULL);
	bt_gatt_client_ready_register(cli->gatt, ready_cb, cli, NULL);
	bt_gatt_client_set_service_changed(cli->gatt, service_changed_cb, cli,NULL);

	/* bt_gatt_client already holds a reference */
	gatt_db_unref(cli->db);

	return cli;
}
//释放链接
static void client_destroy(struct client *cli)
{
	bt_gatt_client_unref(cli->gatt);
	bt_att_unref(cli->att);
	free(cli);
}

static void write_cb(bool success, uint8_t att_ecode, void *user_data)
{
	if (success) 
	{
		printf("\nWrite successful\n");
	} 
}
//写特征值
static void cmd_write_value(uint16_t handle,struct client *cli, char *cmd, int length)
{

	if (!bt_gatt_client_is_ready(cli->gatt)) {
		printf("GATT client not initialized\n");
		return;
	}

#if 0
	if (without_response) {
		if (!bt_gatt_client_write_without_response(cli->gatt, handle,
						signed_write, cmd, length)) {
			printf("Failed to initiate write without response "
								"procedure\n");
			goto done;
		}

		printf("Write command sent\n");
	}
#endif
	if (!bt_gatt_client_write_value(cli->gatt, handle, cmd, length,write_cb,NULL, NULL))
	{
		printf("Failed to initiate write procedure\n");
	}
		

}

//链接device
static int l2cap_le_att_connect(bdaddr_t *src, bdaddr_t *dst, uint8_t dst_type, int sec)
{
	int sock;
	struct sockaddr_l2 srcaddr, dstaddr;
	struct bt_security btsec;
#if 1
	char srcaddr_str[18];
	char dstaddr_str[18];
	

		ba2str(src, srcaddr_str);
		ba2str(dst, dstaddr_str);

		printf("btgatt-client: Opening L2CAP LE connection on ATT "
					"channel:\n\t src: %s\n\tdest: %s\n",
					srcaddr_str, dstaddr_str);

#endif
	sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	if (sock < 0) {
		perror("Failed to create L2CAP socket");
		return -1;
	}

	/* Set up source address */
	memset(&srcaddr, 0, sizeof(srcaddr));
	srcaddr.l2_family = AF_BLUETOOTH;
	srcaddr.l2_cid = htobs(ATT_CID);
	srcaddr.l2_bdaddr_type = 0;
	bacpy(&srcaddr.l2_bdaddr, src);

	if (bind(sock, (struct sockaddr *)&srcaddr, sizeof(srcaddr)) < 0) {
		perror("Failed to bind L2CAP socket");
		close(sock);
		return -1;
	}

	/* Set the security level */
	memset(&btsec, 0, sizeof(btsec));
	btsec.level = sec;
	if (setsockopt(sock, SOL_BLUETOOTH, BT_SECURITY, &btsec,
							sizeof(btsec)) != 0) {
		printf(stderr, "Failed to set L2CAP security level\n");
		close(sock);
		return -1;
	}

	/* Set up destination address */
	memset(&dstaddr, 0, sizeof(dstaddr));
	dstaddr.l2_family = AF_BLUETOOTH;
	dstaddr.l2_cid = htobs(ATT_CID);
	dstaddr.l2_bdaddr_type = dst_type;
	bacpy(&dstaddr.l2_bdaddr, dst);

	printf("Connecting to device...");
	fflush(stdout);
	//printf("-%02x-%02x-%02x-%02x-%02x-%02x-\n",dstaddr.l2_bdaddr.b[0],dstaddr.l2_bdaddr.b[1],dstaddr.l2_bdaddr.b[2],dstaddr.l2_bdaddr.b[3],dstaddr.l2_bdaddr.b[4],dstaddr.l2_bdaddr.b[5]);
	if (connect(sock, (struct sockaddr *) &dstaddr, sizeof(dstaddr)) < 0) {
		perror(" Failed to connect");
		close(sock);
		return -1;
	}
	
	printf(" Done\n");

	return sock;
}
//收到的数据
static void notify_cb(uint16_t value_handle, const uint8_t *value,
					uint16_t length, void *user_data)
{
	int i;

	printf("\n\tHandle Value Not/Ind: 0x%04x - ", value_handle);

	if (length == 0) {
		printf("(0 bytes)\n");
		return;
	}

	printf("(%u bytes): ", length);

	for (i = 0; i < length; i++)
	{
		printf("%02x ", value[i]);
	}
		

}
//注册回调
static void register_notify_cb(uint16_t att_ecode, void *user_data)
{
	if (att_ecode) {
		printf("Failed to register notify handler "
					"- error code: 0x%02x\n", att_ecode);
		return;
	}

	printf("Registered notify handler!\n");
}
//注册notify
static void cmd_register_notify(struct client *cli,uint16_t value_handle)
{
	unsigned int id;

	if (!bt_gatt_client_is_ready(cli->gatt)) {
		printf("GATT client not initialized\n");
		return;
	}

	id = bt_gatt_client_register_notify(cli->gatt, value_handle,
							register_notify_cb,
							notify_cb, NULL, NULL);
	if (!id) {
		printf("Failed to register notify handler\n");
		return;
	}

	printf("Registering notify handler with id: %u\n", id);
}

GMainLoop *loop = NULL;
struct gattlib_thread_t g_gattlib_thread = {0};
void *connection_thread(void* arg)
{
    g_gattlib_thread.loop_context = g_main_context_new();
    g_gattlib_thread.loop = g_main_loop_new(g_gattlib_thread.loop_context, TRUE);

    g_main_loop_run(g_gattlib_thread.loop);
    g_main_loop_unref(g_gattlib_thread.loop);
}
int main()
{
	char *add = "B0:55:08:4D:2A:97";
	//char *add = "CF:7C:50:B1:02:EB";
	int fd;
	int sec = BT_SECURITY_LOW;
	uint16_t mtu = 0;
	uint8_t dst_type = BDADDR_LE_PUBLIC;
	struct client *cli;
	
	bdaddr_t src_addr, dst_addr;
	bacpy(&src_addr, BDADDR_ANY);
	str2ba(add, &dst_addr);
	//mainloop_init();
	loop = g_main_loop_new(NULL,false);
	int error = pthread_create(&g_gattlib_thread.thread, NULL, &connection_thread, &g_gattlib_thread);
	if (error != 0) {
		fprintf(stderr, "Cannot create connection thread: %s", strerror(error));
		return NULL;
	}
    
	/* Wait for the loop to be started */
	while (!g_gattlib_thread.loop || !g_main_loop_is_running (g_gattlib_thread.loop)) 
    {
		usleep(1000);
	}

	//printf("-%02x-%02x-%02x-%02x-%02x-%02x-\n",dst_addr.b[0],dst_addr.b[1],dst_addr.b[2],dst_addr.b[3],dst_addr.b[4],dst_addr.b[5]);
	fd = l2cap_le_att_connect(&src_addr, &dst_addr, dst_type, sec);
	if (fd < 0)
	{
		perror("connect");
	}
	else
	{
		printf("success\n");
	}
	
	cli = client_create(fd, mtu);
	if (!cli) 
	{
		perror("create");
		close(fd);

	}

	//mainloop_run();
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
#if 0
	sleep(1);
	cmd_register_notify(cli, 0x0d);
	sleep(1);
	
	uint16_t handle = 0x10;
	char cmd[20]={0x68,0x03,0x00,0x00,0x6b,0x16};
	cmd_write_value(handle,cli,cmd,6);
	
	sleep(3);
#endif
	//client_destroy(cli);

	return 0;
}

