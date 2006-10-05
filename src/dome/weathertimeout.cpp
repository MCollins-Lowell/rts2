#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../utils/rts2app.h"

class WeatherTimeout:public Rts2App
{
private:
  int timeout;			// in seconds
  int udpPort;
public:
    WeatherTimeout (int in_argc, char **in_argv);

  virtual int processOption (int in_opt);
  virtual int run ();
};

WeatherTimeout::WeatherTimeout (int in_argc, char **in_argv):
Rts2App (in_argc, in_argv)
{
  timeout = 3600;
  udpPort = 5002;

  addOption ('t', "timeout", 1, "timeout (in seconds) to send");
  addOption ('p', "udp_port", 1, "UDP port to which message will be send");
}

int
WeatherTimeout::processOption (int in_opt)
{
  switch (in_opt)
    {
    case 't':
      timeout = atoi (optarg);
      break;
    case 'p':
      udpPort = atoi (optarg);
      break;
    default:
      return Rts2App::processOption (in_opt);
    }
  return 0;
}

int
WeatherTimeout::run ()
{
  int sock;
  struct sockaddr_in bind_addr;
  struct sockaddr_in serv_addr;
  int ret;
  struct hostent *server_info;
  char *send_string;
  int msg_size;

  char buf[4];

  socklen_t size = sizeof (serv_addr);

  sock = socket (PF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      perror ("socket");
      return 1;
    }
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl (INADDR_ANY);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons (udpPort);

  server_info = gethostbyname ("localhost");
  if (!server_info)
    {
      perror ("gethostbyname localhost");
      return 2;
    }

  serv_addr.sin_addr = *(struct in_addr *) server_info->h_addr;

  ret = bind (sock, (struct sockaddr *) &bind_addr, sizeof (bind_addr));
  if (ret < 0)
    {
      perror ("bind");
      return 1;
    }

  msg_size = asprintf (&send_string, "weatherTimeout=%i", timeout);
  ret =
    sendto (sock, send_string, msg_size, 0,
	    (struct sockaddr *) &serv_addr, sizeof (serv_addr));
  if (ret < 0)
    {
      perror ("write");
    }
  ret = recvfrom (sock, buf, 3, 0, (struct sockaddr *) &serv_addr, &size);

  if (ret < 0)
    {
      perror ("recv");
    }

  buf[3] = 0;

  printf ("read %s\n", buf);
  free (send_string);

  close (sock);
  return 0;
}

int
main (int argc, char **argv)
{
  int ret;
  WeatherTimeout *app = new WeatherTimeout (argc, argv);
  ret = app->init ();
  if (!ret)
    ret = app->run ();
  delete app;
  return ret;
}
