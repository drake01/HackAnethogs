#include "nethogs.cpp"

static void versiondisplay(void)
{
	std::cerr << version << "\n";
}

static void help(void)
{
	//std::cerr << "usage: nethogs [-V] [-b] [-d seconds] [-t] [-p] [-f (eth|ppp))] [device [device [device ...]]]\n";
	std::cerr << "usage: nethogs [-V] [-b] [-d seconds] [-t] [-p] [device [device [device ...]]]\n";
	std::cerr << "		-V : prints version.\n";
	std::cerr << "		-d : delay for update refresh rate in seconds. default is 1.\n";
	std::cerr << "		-t : tracemode.\n";
	//std::cerr << "		-f : format of packets on interface, default is eth.\n";
	std::cerr << "		-b : bughunt mode - implies tracemode.\n";
	std::cerr << "		-p : sniff in promiscious mode (not recommended).\n";
	std::cerr << "		device : device(s) to monitor. default is eth0\n";
	std::cerr << std::endl;
	std::cerr << "When nethogs is running, press:\n";
	std::cerr << " q: quit\n";
	std::cerr << " m: switch between total and kb/s mode\n";
}

int main (int argc, char** argv)
{
	process_init();

	device * devices = NULL;
	//dp_link_type linktype = dp_link_ethernet;
	int promisc = 0;

	int opt;
	while ((opt = getopt(argc, argv, "Vhbtpd:")) != -1) {
		switch(opt) {
			case 'V':
				versiondisplay();
				exit(0);
			case 'h':
				help();
				exit(0);
			case 'b':
				bughuntmode = true;
				tracemode = true;
				break;
			case 't':
				tracemode = true;
				break;
			case 'p':
				promisc = 1;
				break;
			case 'd':
				refreshdelay=atoi(optarg);
				break;
			/*
			case 'f':
				argv++;
				if (strcmp (optarg, "ppp") == 0)
					linktype = dp_link_ppp;
				else if (strcmp (optarg, "eth") == 0)
					linktype = dp_link_ethernet;
				}
				break;
			*/
			default:
				help();
				exit(EXIT_FAILURE);
		}
	}

	while (optind < argc) {
		devices = new device (strdup(argv[optind++]), devices);
	}

	if (devices == NULL)
	{
		devices = determine_default_device();
	}

	if ((!tracemode) && (!DEBUG)){
		init_ui();
	}

	if (NEEDROOT && (getuid() != 0))
		forceExit(false, "You need to be root to run NetHogs!");

	char errbuf[PCAP_ERRBUF_SIZE];

	handle * handles = NULL;
	device * current_dev = devices;
	while (current_dev != NULL) {
		getLocal(current_dev->name, tracemode);
		if ((!tracemode) && (!DEBUG)){
			//caption->append(current_dev->name);
			//caption->append(" ");
		}

		dp_handle * newhandle = dp_open_live(current_dev->name, BUFSIZ, promisc, 100, errbuf);
		if (newhandle != NULL)
		{
			dp_addcb (newhandle, dp_packet_ip, process_ip);
			dp_addcb (newhandle, dp_packet_ip6, process_ip6);
			dp_addcb (newhandle, dp_packet_tcp, process_tcp);
			dp_addcb (newhandle, dp_packet_udp, process_udp);

			/* The following code solves sf.net bug 1019381, but is only available
			 * in newer versions (from 0.8 it seems) of libpcap
			 *
			 * update: version 0.7.2, which is in debian stable now, should be ok
			 * also.
			 */
			if (dp_setnonblock (newhandle, 1, errbuf) == -1)
			{
				fprintf(stderr, "Error putting libpcap in nonblocking mode\n");
			}
			handles = new handle (newhandle, current_dev->name, handles);
		}
		else
		{
			fprintf(stderr, "Error opening handler for device %s\n", current_dev->name);
		}

		current_dev = current_dev->next;
	}

	signal (SIGALRM, &alarm_cb);
	signal (SIGINT, &quit_cb);
	alarm (refreshdelay);

	fprintf(stderr, "Waiting for first packet to arrive (see sourceforge.net bug 1019381)\n");

	// Main loop:
	//
	//  Walks though the 'handles' list, which contains handles opened in non-blocking mode.
	//  This causes the CPU utilisation to go up to 100%. This is tricky:
	while (1)
	{
		bool packets_read = false;

		handle * current_handle = handles;
		while (current_handle != NULL)
		{
			struct dpargs * userdata = (dpargs *) malloc (sizeof (struct dpargs));
			userdata->sa_family = AF_UNSPEC;
			currentdevice = current_handle->devicename;
			int retval = dp_dispatch (current_handle->content, -1, (u_char *)userdata, sizeof (struct dpargs));
			if (retval == -1 || retval == -2)
			{
				std::cerr << "Error dispatching" << std::endl;
			}
			else if (retval != 0)
			{
				packets_read = true;
			}
			free (userdata);
			current_handle = current_handle->next;
		}

		if ((!DEBUG)&&(!tracemode))
		{
			// handle user input
			ui_tick();
		}

		if (needrefresh)
		{
			do_refresh();
			needrefresh = false;
		}

		// If no packets were read at all this iteration, pause to prevent 100%
		// CPU utilisation;
		if (!packets_read)
		{
			usleep(100);
		}
	}
}

