#include "led-matrix.h"
#include "error.h"
#include "threaded-canvas-manipulator.h"
#include "utils.h"
#include "font.h"

#include <atomic>
#include <jansson.h>
#include <map>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

using namespace rgb_matrix;

typedef struct {
	std::string font_name, default_font;
	int x, y, w, h;
	int pps, duration, z_depth, alpha;
	bool prio, repeat_wrap, move_left, antialias;
	std::string transparent_color, text;
	uint8_t *output_buffer;
	pthread_mutex_t output_buffer_lock;
	std::atomic_bool terminate;
	std::atomic_int pause;
	pthread_t thread;
	std::atomic_bool *need_update, *want_flash;
} disp_element_t;

std::atomic_bool global_terminate;

void sigh(int sig)
{
	printf("Caught signal %d\n", sig);
	global_terminate = true;
}

typedef struct {
	bool flag;
	uint8_t *data;
	int w, h;
	std::atomic_int *brightness;
	bool screensaver;
	std::atomic_bool need_update, want_flash;
	std::string font_name;
} double_buffer_t;

typedef enum { SS_BROWN, SS_CLOCK } screensaver_t;

class UpdateMatrix : public ThreadedCanvasManipulator {
private:
	double_buffer_t *const db;
	pthread_rwlock_t *const clients_lock;
	std::map<std::string, disp_element_t *> *const clients;
	const int fps;
	Canvas *const c;
	int bytes;
	screensaver_t st;

public:
	UpdateMatrix(RGBMatrix *m, double_buffer_t *const db_in, pthread_rwlock_t *const clients_lock_in, std::map<std::string, disp_element_t *> *const clients_in, int fps_in, const screensaver_t st_in) : ThreadedCanvasManipulator(m), db(db_in), clients_lock(clients_lock_in), clients(clients_in), fps(fps_in), c(canvas()), st(st_in) {
		bytes = db -> w * db -> h * 3;
	}

	void drawBuffer() {
		const int w = c -> width();
		const int h = c -> height();

		for(int y=0; y<h; y++) {
			for(int x=0; x<w; x++) {
				int o = y * db -> w * 3 + x * 3;

				c->SetPixel(x, y, (db -> data[o + 0] * *db -> brightness) / 100, (db -> data[o + 1] * *db -> brightness) / 100, (db -> data[o + 2] * *db -> brightness) / 100);
			}
		}
	}

	void flash() {
		for(int i=0; i<3; i++)
		{
			c -> Fill(*db -> brightness, *db -> brightness, *db -> brightness);
			usleep(100000);
			c -> Clear();
			usleep(75000);
		}
	}

	void drawFromClients(bool *const anything_drawn, bool *const anything_running) {
		pthread_rwlock_rdlock(clients_lock);

		// if there's one or more prio-elements, then do not draw any others
		bool prio = false;
		std::map<std::string, disp_element_t *>::iterator it_prio = clients -> begin();

		for(;it_prio != clients -> end(); it_prio++)
		{
			if (!it_prio -> second -> terminate && !it_prio -> second -> pause)
				prio |= it_prio -> second -> prio;
		}

		*anything_running = !clients -> empty();

		// order items by z-depth
		std::map<int, disp_element_t *> depth_map;

		std::map<std::string, disp_element_t *>::iterator it_copy = clients -> begin();
		for(;it_copy != clients -> end(); it_copy++)
		{
			if ((!prio || (prio && it_copy -> second -> prio)) && !(it_copy -> second -> terminate || it_copy -> second -> pause))
				depth_map.insert(std::pair<int, disp_element_t *>(it_copy -> second -> z_depth, it_copy -> second));
		}

		memset(db -> data, 0x00, bytes);

		// iterate through z-ordered map and draw
		std::map<int, disp_element_t *>::iterator dmo_it = depth_map.begin();
		for(;dmo_it != depth_map.end(); dmo_it++)
		{
			pthread_mutex_lock(&dmo_it -> second -> output_buffer_lock);

			bitblit(db -> data, db -> w, db -> h, dmo_it -> second -> x, dmo_it -> second -> y, dmo_it -> second -> output_buffer, dmo_it -> second -> w, dmo_it -> second -> h, 0, 0, dmo_it -> second -> w, dmo_it -> second -> h, dmo_it -> second -> transparent_color, dmo_it -> second -> alpha);

			pthread_mutex_unlock(&dmo_it -> second -> output_buffer_lock);
		}

		pthread_rwlock_unlock(clients_lock);

		*anything_drawn = !depth_map.empty();

		// printf("prio: %d, anything running: %d, any draws: %d\n", prio, *anything_running, *anything_drawn);
	}

	void draw_centered(const std::string & text)
	{
		memset(db -> data, 0x00, bytes);

		font f(db -> font_name, text, c -> height(), true);

		bool flash_requested = false;
		uint8_t *text_img = NULL;
		int text_w = 0;
		f.getImage(&text_w, &text_img, &flash_requested);
		int max_ascender = f.getMaxAscender();

		int y = db -> h / 2 - (max_ascender / 64) / 2;
		if (y < 0)
			y = 0;

		int x = db -> w / 2 - text_w / 2;
		if (x < 0)
			x = 0;

		bitblit(db -> data, db -> w, db -> h, x, y, text_img, text_w, c -> height(), 0, 0, text_w, c -> height(), "", -1);
	}

	bool screensaver() {
		if (st == SS_BROWN) {
			memset(db -> data, 0x00, bytes);

			static int ssx = 0, ssy = 0;

			db -> data[ssy * db -> w * 3 + ssx * 3] = 255;

			ssx += (rand() % 3) - 1;

			if (ssx < 0)
				ssx = 0;
			else if (ssx >= db -> w)
				ssx = db -> w - 1;

			ssy += (rand() % 3) - 1;

			if (ssy < 0)
				ssy = 0;
			else if (ssy >= db -> h)
				ssy = db -> h - 1;

			return true;
		}
		else if (st == SS_CLOCK) {
			static bool which = false;
			static time_t prev_ts_switch = 0, prev_ts_tick = 0;
			time_t now = time(NULL);

			if (now - prev_ts_tick >= 1) {
				std::string text;

				if (which) {
					struct tm *tm = localtime(&now);
					text = format("$r%02d:%02d:%02d", tm -> tm_hour, tm -> tm_min, tm -> tm_sec);
				}
				else {
					struct tm *tm = localtime(&now);
					text = format("$r%04d-%02d-%02d", tm -> tm_year + 1900, tm -> tm_mon + 1, tm -> tm_mday);
				}

				draw_centered(text);

				prev_ts_tick = now;

				if (now - prev_ts_switch >= 2) {
					which = !which;
					prev_ts_switch = now;
				}

				return true;
			}
		}

		return false;
	}

	void Run() {
		printf("display_updater thread started\n");

		set_thread_name(pthread_self(), "display_updater");

		const int us_for_fps = MILLION / fps;
		const int64_t start = get_ts();
		const int bytes = db -> w * db -> h * 3;

		for(;!global_terminate;) {
			memset(db -> data, 0x00, bytes);

			if (db -> want_flash.exchange(false))
				flash();

			bool anything_drawn = false, anything_running = false;
			if (db -> need_update.exchange(false)) {
				// printf("need_update true\n");
				drawFromClients(&anything_drawn, &anything_running);

				if (!anything_running) {
					c -> Clear();
					anything_drawn = true;
				}
			}

			if (db -> screensaver && anything_drawn == false)
				anything_drawn = screensaver();


			if (anything_drawn)
				drawBuffer();

			int64_t now = get_ts();
			int64_t sleep_left = us_for_fps - ((now - start) % us_for_fps);

			if (sleep_left > 0)
				usleep(sleep_left);
		}

		c -> Clear();

		printf("display_updater thread terminating\n");
	}
};

void *run_display_element(void *p)
{
	printf("thread started\n");
	disp_element_t *const de = (disp_element_t *)p;

	std::string font_file = find_font_by_name(de -> font_name, de -> default_font);
	font f(font_file, de -> text, de -> h, de -> antialias);

	bool flash_requested = false;
	int text_w = 0, text_h = de -> h;
	uint8_t *text_img = NULL;
	f.getImage(&text_w, &text_img, &flash_requested);

	bool paused = de -> pause;
	printf("text width after render: %d, pause: %d\n", text_w, paused);

	const int64_t start = get_ts();
	const int64_t us_per_pps = MILLION / de -> pps;

	if (flash_requested)
		*de -> want_flash = true;

	int x = 0;
	do
	{
		if (!de -> pause)
		{
			int wx = x, plotted_n = 0;

			//printf("\n");
			int copy_n = text_w - wx;
			do
			{
				//printf("disp:%d/text:%d | sx:%d dx:%d cn:%d\n", de -> w, text_w, wx, plotted_n, copy_n);
				pthread_mutex_lock(&de -> output_buffer_lock);
				bitblit(de -> output_buffer, de -> w, de -> h, plotted_n, 0, text_img, text_w, text_h, wx, 0, copy_n, text_h, "", -1);
				pthread_mutex_unlock(&de -> output_buffer_lock);

				wx += copy_n;
				while(wx >= text_w)
					wx -= text_w;
				plotted_n += copy_n;

				copy_n = text_w;
			}
			while(plotted_n < de -> w && de -> repeat_wrap);

			if (de -> move_left)
			{
				x++;

				while(x >= text_w)
					x -= text_w;
			}
			else
			{
				x--;

				while(x < 0)
					x += text_w;
			}

			*de -> need_update = true;
		}

		int64_t now = get_ts();
		int64_t sleep_left = us_per_pps - ((now - start) % us_per_pps);

		if (sleep_left > 0)
			usleep(sleep_left);
	}
	while(de -> terminate == false && (de -> duration == 0 || get_ts() - start < de -> duration));

	printf("thread for \"%s\" terminating\n", de -> text.c_str());

	pthread_mutex_lock(&de -> output_buffer_lock);
	memset(de -> output_buffer, 0x00, de -> w * de -> h * 3);
	pthread_mutex_unlock(&de -> output_buffer_lock);

	de -> terminate = true;

	return NULL;
}

void pause_all_but(pthread_rwlock_t *const clients_lock, std::map<std::string, disp_element_t *> *const clients, const std::string & skip, const bool pause)
{
	pthread_rwlock_rdlock(clients_lock); // readlock: not changing the map, only the data of the map

	std::map<std::string, disp_element_t *>::iterator it = clients -> begin();
	for(; it != clients -> end(); it++)
	{
		if (it -> first != skip)
		{
			if (pause)
				it -> second -> pause++;
			else if (it -> second -> pause > 0)
				it -> second -> pause--;
		}
	}

	pthread_rwlock_unlock(clients_lock);
}

bool purge_threads(pthread_rwlock_t *const clients_lock, std::map<std::string, disp_element_t *> *const clients)
{
	bool hits = false;

	pthread_rwlock_wrlock(clients_lock);

	std::map<std::string, disp_element_t *>::iterator it = clients -> begin();
	for(; it != clients -> end();)
	{
		if (it -> second -> terminate == false) {
			it++;
			continue;
		}

		void *dummy = NULL;
		int rc = pthread_join(it -> second -> thread, &dummy);
		if (rc == 0)
		{
			pthread_mutex_destroy(&it -> second -> output_buffer_lock);
			delete [] it -> second -> output_buffer;
			delete it -> second;

			clients -> erase(it++);

			hits = true;
		}
		else
		{
			it++;
		}
	}

	pthread_rwlock_unlock(clients_lock);

	return hits;
}

void terminate_threads(pthread_rwlock_t *const clients_lock, std::map<std::string, disp_element_t *> *const clients)
{
	pthread_rwlock_rdlock(clients_lock);

	std::map<std::string, disp_element_t *>::iterator it = clients -> begin();
	for(; it != clients -> end(); it++)
	{
		it -> second -> terminate = true;

		void *dummy = NULL;
		pthread_join(it -> second -> thread, &dummy);
	}

	pthread_rwlock_unlock(clients_lock);

	purge_threads(clients_lock, clients); // clean-up
}

void process_json_request(const std::string & msg, double_buffer_t *const db, pthread_rwlock_t *const clients_lock, std::map<std::string, disp_element_t *> *const clients, std::atomic_int *const brightness, std::atomic_bool *const need_update)
{
	json_error_t error;
	json_t *obj = json_loads(msg.c_str(), msg.size(), &error);
	if (!obj)
	{
		fprintf(stderr, "JSON data failed to parse: %s", error.text);
		return;
	}

	printf("JSON parsed: %s\n", json_dumps(obj, JSON_INDENT(2)));

	std::string cmd = get_json_str(obj, "cmd", "?");

	if (cmd == "add_text")
	{
		std::string id = get_json_str(obj, "id", "");

		if (id.empty())
		{
			id = format("%x%x", rand(), rand());
			fprintf(stderr, "No id given, using %s\n", id.c_str());
		}

		pthread_rwlock_wrlock(clients_lock);
		std::map<std::string, disp_element_t *>::iterator it = clients -> find(id);

		if (it != clients -> end())
		{
			disp_element_t *de = it -> second;
			de -> pause = de -> terminate = true;

			clients -> erase(it);

			clients -> insert(std::pair<std::string, disp_element_t *>(id + format("_%d_terminate", rand()), de));
		}

		disp_element_t *de = new disp_element_t;
		de -> x = get_json_int(obj, "x", 0);
		check_range(&de -> x, 0, db -> w - 1);
		de -> y = get_json_int(obj, "y", 0);
		check_range(&de -> y, 0, db -> h * 2);
		de -> w = get_json_int(obj, "w", db -> w);
		check_range(&de -> w, 1, db -> w);
		de -> h = get_json_int(obj, "h", db -> h);
		check_range(&de -> h, 1, db -> h);
		de -> text = get_json_str(obj, "text", "no text given");
		de -> pps = get_json_int(obj, "pps", 10); // pixels per second
		check_range(&de -> pps, 1, db -> w);
		de -> duration = get_json_int(obj, "duration", 0) * 1000; // in ms
		de -> z_depth = get_json_int(obj, "z_depth", 0); // z-depth: 255 is front
		check_range(&de -> z_depth, 0, 255);
		de -> pause = 0;
		de -> prio = get_json_int(obj, "prio", 1) != 0;
		de -> repeat_wrap = get_json_int(obj, "repeat_wrap", 1) != 0;
		de -> move_left = get_json_int(obj, "move_left", 1) != 0;
		de -> terminate = false;
		de -> output_buffer = new uint8_t[de -> w * de -> h * 3];
		de -> output_buffer_lock = PTHREAD_MUTEX_INITIALIZER;
		de -> need_update = need_update;
		de -> want_flash = &db -> want_flash;
		de -> font_name = get_json_str(obj, "font_name", db -> font_name);
		de -> default_font = db -> font_name;
		de -> transparent_color = get_json_str(obj, "transparent_color", "");
		if (de -> transparent_color.empty());
		de -> transparent_color = get_json_str(obj, "transparency_color", "");
		de -> alpha = get_json_int(obj, "alpha", -1);
		de -> antialias = get_json_int(obj, "antialias", 1) != 0;

		clients -> insert(std::pair<std::string, disp_element_t *>(id, de));

		pthread_create(&de -> thread, NULL, run_display_element, de);
		set_thread_name(de -> thread, "t" + id);

		pthread_rwlock_unlock(clients_lock);

		fprintf(stderr, "Started text-scroller with id %s\n", id.c_str());
	}
	else if (cmd == "stop")
	{
		std::string id = get_json_str(obj, "id", "");

		pthread_rwlock_wrlock(clients_lock);
		std::map<std::string, disp_element_t *>::iterator it = clients -> find(id);

		if (it != clients -> end())
		{
			fprintf(stderr, "stopping %s\n", id.c_str());
			it -> second -> terminate = true;
		}
		else
		{
			fprintf(stderr, "id %s not found for %s\n", id.c_str(), cmd.c_str());
		}
		pthread_rwlock_unlock(clients_lock);
	}
	else if (cmd == "stop-all")
	{
		pthread_rwlock_wrlock(clients_lock);
		std::map<std::string, disp_element_t *>::iterator it = clients -> begin();

		for(;it != clients -> end(); it++)
			it -> second -> terminate = true;

		pthread_rwlock_unlock(clients_lock);
	}
	else if (cmd == "brightness")
	{
		*brightness = get_json_int(obj, "brightness", 100);
	}
	else if (cmd == "terminate")
	{
		fprintf(stderr, "terminating application\n");
		global_terminate = true;
	}
	else
	{
		fprintf(stderr, "command %s not known\n", cmd.c_str());
	}
}

typedef struct
{
	double_buffer_t *db;
	pthread_rwlock_t *clients_lock;
	std::map<std::string, disp_element_t *> *clients;
	std::atomic_int *brightness;
	std::atomic_bool *need_update;
	int listen_port;
} listener_thread_pars_t;

void *udp_listener(void *p)
{
	listener_thread_pars_t *const ltp = (listener_thread_pars_t *)p;

	int udp_fd = start_listening_udp(ltp -> listen_port);
	printf("UDP listener started for port %d\n", ltp -> listen_port);

	struct pollfd fds[1] = { { udp_fd, POLLIN, 0 } };

	for(;!global_terminate;)
	{
		if (purge_threads(ltp -> clients_lock, ltp -> clients))
			*ltp -> need_update = true;

		fds[0].revents = 0;

		if (poll(fds, 1, 250) == 0)
			continue;

		char buffer[65536];
		int rc = recv(udp_fd, buffer, sizeof buffer, 0);

		if (rc == -1)
		{
			if (errno == EINTR)
				break;

			error_exit(true, "recv() failed");
		}

		if (rc == 0) // this would be odd, at least for dgram
		{
			fprintf(stderr, "EOF on dgram socket?!\n");
			break;
		}

		buffer[rc] = 0x00;

		process_json_request(buffer, ltp -> db, ltp -> clients_lock, ltp -> clients, ltp -> brightness, ltp -> need_update);
	}

	return NULL;
}

typedef struct
{
	listener_thread_pars_t *ltp;
	int client_fd;
} tcp_handler_pars_t;

void *handle_tcp_connection(void *p)
{
	tcp_handler_pars_t *thp = (tcp_handler_pars_t *)p;
	listener_thread_pars_t *const ltp = thp -> ltp;

	std::string json_str;
	struct pollfd fds[1] = { { thp -> client_fd, POLLIN, 0 } };

	for(;!global_terminate;)
	{
		fds[0].revents = 0;

		if (poll(fds, 1, 251) == 0)
			continue;

		char buffer[4096];
		int rc = read(thp -> client_fd, buffer, sizeof(buffer));

		if (rc == -1)
		{
			if (errno == EINTR)
				continue;

			break;
		}

		if (rc == 0)
			break;

		json_str += std::string(buffer, rc);
	}

	close(thp -> client_fd);

	process_json_request(json_str, ltp -> db, ltp -> clients_lock, ltp -> clients, ltp -> brightness, ltp -> need_update);

	delete thp;

	return NULL;
}

void *tcp_listener(void *p)
{
	listener_thread_pars_t *const ltp = (listener_thread_pars_t *)p;

	int server_fd = start_listening_tcp(ltp -> listen_port);
	struct pollfd fds[1] = { { server_fd, POLLIN, 0 } };
	printf("TCP listener started for port %d\n", ltp -> listen_port);

	for(;!global_terminate;)
	{
		if (purge_threads(ltp -> clients_lock, ltp -> clients))
			*ltp -> need_update = true;

		fds[0].revents = 0;

		if (poll(fds, 1, 251) == 0)
			continue;

		int client_fd = accept(server_fd, NULL, NULL);
		if (client_fd == -1)
		{
			fprintf(stderr, "accept failed: %s\n", strerror(errno));
			usleep(1000);
			continue;
		}

		pthread_attr_t ta;
		pthread_attr_init(&ta);
		pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);

		tcp_handler_pars_t *thp = new tcp_handler_pars_t;
		thp -> ltp = ltp;
		thp -> client_fd = client_fd;

		pthread_t tcp_process_th;
		pthread_create(&tcp_process_th, &ta, handle_tcp_connection, thp);
	}

	close(server_fd);

	return NULL;
}

void main_loop(double_buffer_t *const db, pthread_rwlock_t *const clients_lock, std::map<std::string, disp_element_t *> *const clients, std::atomic_int *const brightness, std::atomic_bool *const need_update, const int listen_port)
{
	listener_thread_pars_t ltp;

	ltp.db = db;
	ltp.clients_lock = clients_lock;
	ltp.clients = clients;
	ltp.brightness = brightness;
	ltp.need_update = need_update;
	ltp.listen_port = listen_port;

	pthread_t udp_listener_th;
	pthread_create(&udp_listener_th, NULL, udp_listener, &ltp);

	pthread_t tcp_listener_th;
	pthread_create(&tcp_listener_th, NULL, tcp_listener, &ltp);

	void *dummy = NULL;
	pthread_join(tcp_listener_th, &dummy);
	pthread_join(udp_listener_th, &dummy);
}

void help(void)
{
	printf("-r <rows>      : Display rows. 16 for 16x32, 32 for 32x32. Default 32\n");
	printf("-c <chained>   : Daisy-chained boards. Default: 1\n");
	printf("-p <pwm-bits>  : Bits used for PWM. Something between 1..11\n");
	printf("-l             : Toggle \"luminance correction\". Default on\n");
	printf("-s             : Enable screensaver\n");
	printf("-P <port>      : TCP/UDP port to listen on\n");
	printf("-b <brightness>: Set brightness (1...100). Default: 50\n");
	printf("-f <fps>       : Refresh-rate. Default: 50\n");
	printf("-d             : Fork into the background\n");
	printf("-F <font>      : Default font name (file, not name!). Default: %s", DEFAULT_FONT_FILE);
}

int main(int argc, char *argv[]) {
	global_terminate = false;

	bool correct_luminance = true, screensaver = false, do_fork = false;
	int rows_on_display = 32, chained_displays = 1, pwm_bits = 0, brightness_in = 50, fps = 50;
	int listen_port = 3333;
	screensaver_t ss = SS_CLOCK; // FIXME commandline selectable

	std::string default_font = DEFAULT_FONT_FILE;

	int c = -1;
	while((c = getopt(argc, argv, "p:r:c:t:lsP:b:f:dF:h")) != -1)
	{
		switch(c)
		{
			case 'p':
				pwm_bits = atoi(optarg);
				break;

			case 'r':
				rows_on_display = atoi(optarg);
				break;

			case 'c':
				chained_displays = atoi(optarg);
				break;

			case 't':
				pwm_bits = atoi(optarg);
				break;

			case 'l':
				correct_luminance = !correct_luminance;
				break;

			case 's':
				screensaver = true;
				break;

			case 'P':
				listen_port = atoi(optarg);
				break;

			case 'b':
				brightness_in = atoi(optarg);
				break;

			case 'f':
				fps = atoi(optarg);
				break;

			case 'd':
				do_fork = true;
				break;

			case 'F':
				default_font = optarg;
				break;

			case 'h':
				help();
				return 0;

			default:
				help();
				error_exit(false, "option %c is not understood", c);
		}
	}

	signal(SIGINT, sigh);
	signal(SIGTERM, sigh);

	srand(time(NULL));

	font::init_fonts();

	GPIO io;
	if (!io.Init())
		error_exit(false, "Failed to initialized GPIO sub system");

	srand(time(NULL));

	RGBMatrix m(&io, rows_on_display, chained_displays, 1);

	if (pwm_bits > 0 && !m.SetPWMBits(pwm_bits))
		error_exit(false, "Invalid range of pwm-bits");

	if (correct_luminance)
		m.set_luminance_correct(true);

	std::atomic_int brightness(brightness_in);

	double_buffer_t db;
	db.w = m.width(); // change for different panel layout
	db.h = m.height();
	int pixel_bytes = db.w * db.h * 3;
	db.data = new uint8_t[pixel_bytes];
	db.brightness = &brightness;
	db.flag = db.need_update = false;
	db.screensaver = screensaver;
	db.font_name = default_font;

	pthread_rwlock_t clients_lock;
	pthread_rwlock_init(&clients_lock, NULL);

	std::map<std::string, disp_element_t *> clients;

	ThreadedCanvasManipulator *image_gen = new UpdateMatrix(&m, &db, &clients_lock, &clients, fps, ss);

	image_gen->Start();

	if (do_fork && daemon(0, 0) == -1)
		error_exit(true, "Failed to daemon()");

	printf("Go!\n");

	main_loop(&db, &clients_lock, &clients, &brightness, &db.need_update, listen_port);

	terminate_threads(&clients_lock, &clients);

	global_terminate = true;
	image_gen->Stop();

	// Stopping threads and wait for them to join.
	delete image_gen;

	font::uninit_fonts();

	printf("END\n");

	return 0;
}
