#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "error.h"
#include "utils.h"

int hex_to_val(char c)
{
	c = tolower(c);

	if (c >= 'a')
		return c - 'a' + 10;

	return c - '0';
}

std::string format(const char *const fmt, ...)
{
	char *buffer = NULL;
        va_list ap;

	std::string result;

        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) != -1)
		result = buffer;
        va_end(ap);

	free(buffer);

	return result;
}

int64_t get_ts()
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == -1)
		error_exit(true, "gettimeofday failed");

	return tv.tv_sec * MILLION + tv.tv_usec;
}

void set_thread_name(const pthread_t th, const std::string & name)
{
	if (name.size() > 15)
		pthread_setname_np(th, name.substr(0, 15).c_str());
	else
		pthread_setname_np(th, name.c_str());
}

void hex_str_to_rgb(const std::string & in, uint8_t *const r, uint8_t *const g, uint8_t *const b)
{
	if (in.size() >= 6)
	{
		std::string work = in.size() == 7 ? in.substr(1) : in;

		*r = (hex_to_val(work.at(0)) << 4) | hex_to_val(work.at(1));
		*g = (hex_to_val(work.at(2)) << 4) | hex_to_val(work.at(3));
		*b = (hex_to_val(work.at(4)) << 4) | hex_to_val(work.at(5));
	}
}

void bitblit(uint8_t *const target, const int tw, const int th, const int tx, const int ty, const uint8_t *const source, const int sw, const int sh, const int sx, const int sy, const int scw, const int sch, const std::string & transparent_color, const int alpha)
{
	int source_space_x = std::max(0, sw - sx);
	int source_space_y = std::max(0, sh - sy);
	int target_space_x = std::max(0, tw - tx);
	int target_space_y = std::max(0, th - ty);
	int copy_w = std::min(std::min(std::min(tw, scw), source_space_x), target_space_x);
	int copy_h = std::min(std::min(std::min(th, sch), source_space_y), target_space_y);

	// printf("copy %dx%d pixels to %d,%d from %d,%d\n", copy_w, copy_h, tx, ty, sx, sy);
	for(int y=0; y<copy_h; y++)
	{
		if (!transparent_color.empty() || alpha >= 0)
		{
			uint8_t tr_r = 0, tr_g = 0, tr_b = 0;
			hex_str_to_rgb(transparent_color, &tr_r, &tr_g, &tr_b);

			for(int x=0; x<copy_w; x++) {
				int source_offset = (sx + x) * 3 + (sy + y) * sw * 3;
				int target_offset = (tx + x) * 3 + (ty + y) * tw * 3;

				bool transparent_copy = transparent_color.empty() || source[source_offset + 0] != tr_r || source[source_offset + 1] != tr_g || source[source_offset + 2] != tr_b;
				if (!transparent_copy)
					continue;

				if (alpha >= 0)
				{
					target[target_offset + 0] = (source[source_offset + 0] * alpha + target[target_offset + 0] * (100 - alpha)) / 100;
					target[target_offset + 1] = (source[source_offset + 1] * alpha + target[target_offset + 0] * (100 - alpha)) / 100;
					target[target_offset + 2] = (source[source_offset + 2] * alpha + target[target_offset + 0] * (100 - alpha)) / 100;
				}
				else
				{
					target[target_offset + 0] = source[source_offset + 0];
					target[target_offset + 1] = source[source_offset + 1];
					target[target_offset + 2] = source[source_offset + 2];
				}
			}
		}
		else
		{
			for(int x=0; x<copy_w; x++) {
				int source_offset = (sx + x) * 3 + (sy + y) * sw * 3;
				int target_offset = (tx + x) * 3 + (ty + y) * tw * 3;

				target[target_offset + 0] = source[source_offset + 0];
				target[target_offset + 1] = source[source_offset + 1];
				target[target_offset + 2] = source[source_offset + 2];
			}
		}
	}
}

void check_range(int *const chk_val, const int min, const int max)
{
	if (*chk_val < min)
		*chk_val = min;
	else if (*chk_val > max)
		*chk_val = max;
}

std::string get_json_str(const json_t *const j, const std::string & key, const std::string & default_value)
{
        json_t *obj_json = json_object_get(j, key.c_str());
        if (!obj_json)
		return default_value;

        return json_string_value(obj_json);
}

int get_json_int(const json_t *const j, const std::string & key, const int default_value)
{
        json_t *obj_json = json_object_get(j, key.c_str());
        if (!obj_json)
		return default_value;

	if (json_is_integer(obj_json))
		return json_integer_value(obj_json);

        return int(json_number_value(obj_json));
}

int start_listening_udp(const int port) {
	struct sockaddr_in name;

	int fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
		error_exit(true, "Failed creating socket");

	int set = 1;
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof set);

	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&name, sizeof(name)) == -1)
		error_exit(true, "Bind failed");

	return fd;
}

int start_listening_tcp(int port)
{
        struct sockaddr_in server_addr;
        memset(&server_addr, 0x00, sizeof(server_addr));

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        int server_fd = socket(PF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
                error_exit(true, "Creating socket failed");

        if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr)) == -1)
                error_exit(true, "Binding socket to %d failed");

	if (listen(server_fd, SOMAXCONN) == -1)
                error_exit(true, "Listen() socket failed");

	return server_fd;
}

double hue_to_rgb(const double m1, const double m2, double h)
{
	while(h < 0.0)
		h += 1.0;

	while(h > 1.0)
		h -= 1.0;

	if (6.0 * h < 1.0)
		return (m1 + (m2 - m1) * h * 6.0);

	if (2.0 * h < 1.0)
		return m2;

	if (3.0 * h < 2.0)
		return (m1 + (m2 - m1) * ((2.0 / 3.0) - h) * 6.0);

	return m1;
}

void hls_to_rgb(const double H, const double L, const double S, double *const r, double *const g, double *const b)
{
	if (S == 0)
	{
		*r = *g = *b = L;
		return;
	}

	double m2 = 0;

	if (L <= 0.5)
		m2 = L * (1.0 + S);
	else
		m2 = L + S - L * S;

	double m1 = 2.0 * L - m2;

	*r = hue_to_rgb(m1, m2, H + 1.0/3.0);
	*g = hue_to_rgb(m1, m2, H);
	*b = hue_to_rgb(m1, m2, H - 1.0/3.0);
}
