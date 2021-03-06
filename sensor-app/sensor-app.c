/*
 * Copyright (C) 2015, www.easyiot.com.cn
 *
 * The right to copy, distribute, modify, or otherwise make use
 * of this software may be licensed only pursuant to the terms
 * of an applicable license agreement.
 *
 * Config file is in json format, includes the following nodes:
 *   host: Where the agent is on.
 *   port: Which port the agent is listening on
 *   datapoints: The datapoints managed by this datapoint application. The
 *   	initial datapoints node contains the properties of the datapoints.
 *   	After the datapoints are once registered to the agent, each datapoint
 *   	is allocated with one extra fields: id. So the next time this sensor app
 *   	starts, it knows that these datapoints are already registered
 *   	and will only let agent know that it is online.
 */

#define _GNU_SOURCE // required by asprintf

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/timeb.h>
#include <math.h>
#include "lib_sensor.h"
#include "ggpio.h"

#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#define BAUDRATE B9600
#define SERISL_DEVICE "/dev/ttyUSB0"

#define _POSIX_SOURCE 1 /* POSIX compliant source */

const char *default_cfg = "sensor-app.json";

void usage()
{
	printf("Usage: [-h] [-f] [-c <configuration-file>]\n\n"
			"Options:\n"
			"  -c Configuration file.\n"
			"  -h Print this Help\n");
}

static struct termios oldtio;
static int init_serial_port(const char *port)
{
	int fd;
	struct termios newtio;

	fd = open(port, O_RDWR | O_NOCTTY );
	if (fd < 0) {
		perror(port);
		return -1;
	}

	/* save current port settings */
	tcgetattr(fd, &oldtio);
	bzero(&newtio, sizeof(newtio));

	newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;

	/* set input mode (non-canonical, no echo,...) */
	newtio.c_lflag = 0;
	/* inter-character timer unused */
	newtio.c_cc[VTIME]    = 0;
	/* blocking read until 5 chars received */
	newtio.c_cc[VMIN]     = 1;

	tcflush(fd, TCIFLUSH);

	tcsetattr(fd,TCSANOW,&newtio);

	return fd;
}

static int clean_serial_port(int fd)
{
	tcsetattr(fd, TCSANOW, &oldtio);
	close(fd);

	return 0;
}

static void dump_str(unsigned char *cmd, int len)
{
	int i = 0;
	printf("buf_len: %d\n", len);

	while(i < len) {
		printf("%02x ", cmd[i++]);
	}
	printf("\n");
}

static int do_read_from_serial(unsigned char *buf, int fd)
{
	/* file descriptor set */
	fd_set readfds;
	int fdmax = fd;
	int ret = 0;
	int buf_len = 0;
	int res;

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	ret = select(fdmax + 1, &readfds, NULL, NULL, NULL);
	if (ret != 0) {
		if (FD_ISSET(fd, &readfds)) {
			buf_len = 0;

			do {
				/* 5s timeout */
				struct timeval timeout = {5, 0};
				ret = select(fdmax + 1, &readfds, NULL, NULL, &timeout);

				if (ret != 1 && buf_len > 49) {
					/* timeout */
					break;
				}

				res = read(fd, buf + buf_len, 5);
				/* returns after 5 chars have been input */
				buf_len += res;
			} while (buf_len < 50);
		}
	}

	return buf_len;
}

/*
 * Get datapoint data according to datapoint properties.
 * Here we fake the data using random numbers.
 */
void * get_datapoint_data(void *props)
{
	float temperature = 0.0;
	void *ret = NULL;
	const char *name = get_string_by_name(props, "name");
	ret = malloc(sizeof(double));
	if (ret == NULL) {
		perror("No memory available.\n");
		exit(-1);
	}

	if (strcmp(name, "grove_temperature") == 0) {
		/* the temperature sensor's output connect to a0 pin of
		   galileo */
		int a0v = galileo_analog_read(0);
		/* the value of a0v should within [0,4096], that use 12bit to
		 * moderize 0~5v voltage, which means 0 stands for 0v while
		 * 4096 stands for 5v. */
		printf("Readed a0 pin voltage: %1.2f\n", ((double)a0v * 5) / 4096);

		/* then next we'll need to calculate temperature based on
		 * the design of temperature sensor.
		 */
		int val = a0v / 4;
		int B = 3975;
		float resistance;
		if (val != 0) {
			resistance = (float)(1023 - val) * 10000 / val;
			temperature = 1 / (log(resistance / 10000) / B + 1 / 298.15) - 273.15;
		}
		printf("The temperature is: %2.2f c\n", temperature);
		/* return the temperature to libsensor */
		*(double *)ret = (double)temperature;
	} else if (strcmp(name, "grove_light") == 0) {
		int a1v = galileo_analog_read(1);
		printf("Readed a1 pin voltage: %1.2f\n", ((double)a1v * 5) / 4096);

		int val = a1v / 4;
		printf("The light number is: %2.2f\n", (double)val);
		*(double *)ret = (double)val;
	} else if (strcmp(name, "grove_sound") == 0) {
		int a2v = galileo_analog_read(2);
		printf("Readed a2 pin voltage: %1.2f\n", ((double)a2v * 5) / 4096);

		int val = a2v / 4;
		printf("The sound number is: %2.2f\n", (double)val);
		*(double *)ret = (double)val;
	} else if (strcmp(name, "lm35-temperature") == 0) {
		int a0v = galileo_analog_read(0);
		/* get voltage */
		printf("Readed a0 pin voltage: %1.2f\n", ((double)a0v * 5) / 4096);

		double val = a0v / 4;
		/* the lm35 output voltage is 10mV per degree, from 0 to 100 C */
		double temperature = (val * 5 / 1024) * 100.00;
		printf("The lm35 temperature is: %2.2f C\n", temperature);
		*(double *)ret = temperature;
	} else if (strcmp(name, "oc_image") == 0) {
		struct timeb t;
		ftime(&t);
		/* prepare a image file then return its name to libsensor */
		char *file = NULL;
		char *cmd = NULL;
		/* note, according to asprintf, the 'file' need be freed
		   later, which will be done by libsensor */
		asprintf(&file, "image_%lld%s", 1000 * (long long)t.time + t.millitm, ".jpg");
		asprintf(&cmd, "capture %s 2>/dev/null", file);
		system(cmd);
		free(cmd);
		cmd = NULL;
		ret = (void*)file;
	} else if (strcmp(name, "image") == 0) {
		struct timeb t;
		ftime(&t);
		/* prepare a image file then return its name to libsensor */
		char *file = NULL;
		char *cmd = NULL;
		/* note, according to asprintf, the 'file' need be freed
		   later, which will be done by libsensor */
		asprintf(&file, "image_%lld%s", 1000 * (long long)t.time + t.millitm, ".jpg");
		asprintf(&cmd, "fswebcam -r 1280x720 --save %s 2>/dev/null", file);
		system(cmd);
		free(cmd);
		cmd = NULL;
		ret = (void*)file;
	} else if (strcmp(name, "serial_test") == 0) {
		unsigned char buf[32] = {};
		int fd = init_serial_port(SERISL_DEVICE);
		if (fd < 0) {
			*(double *)ret = 0;
		} else {
			int len = do_read_from_serial(buf, fd);
			dump_str(buf, len);
			*(double *)ret = 1;
		}
		clean_serial_port(fd);
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int ch = 0;
	const char *config_file = default_cfg;

	/* command line arguments processing */
	while ((ch=getopt(argc, argv, "hc:f")) != EOF) {
		switch (ch) {
			case 'h':
				usage();
				exit(0);
			case 'c':
				config_file = optarg;
				break;
			default:
				usage();
				exit(1);
		}
	}

	lib_sensor_start(config_file, get_datapoint_data, NULL, NULL);

	printf("sensor app is terminated!\n");
	return 0;
}
