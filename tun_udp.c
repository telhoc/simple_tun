#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <pthread.h>

int tun_fd, sock_fd;
struct ifreq ifr;
struct sockaddr_in addr;
char target_address[24];

#define BUFSIZE 5000 // For each IP packet
#define PORT 2837

int write_tun_kernel(const void *p, size_t size_packet)
{

    int n_tun_write;

    if ((n_tun_write = write(tun_fd, p, size_packet)) < 0)
    {
        // exit(1);
        perror("Error Writing Packet to TUN interface");
    }
    else
    {
        // printf("Wrote %d to TUN interface \n", n_tun_write);
    }

    return n_tun_write;
}

void *read_udp_packets(void *args)
{
    char buf[1024];

    while (1)
    {
        printf("Wait for UDP packets \n");
        int n = recvfrom(sock_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 0)
        {
            perror("recvfrom");
            break;
        }
        else
        {
            printf("Got UDP packet give to kernel\n");
            write_tun_kernel(buf, n);
        }

        // Do something with the data...
    }

    return NULL;
}

void send_udp_packet(char *buf, int len, char *ip, int port)
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        perror("socket");
        return;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    sendto(sock_fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_fd);
}

int tun_alloc(char *dev, int flags)
{

    struct ifreq ifr;
    int fd, err;
    char *opendev = "/dev/net/tun";

    if ((fd = open(opendev, O_RDWR)) < 0)
    {
        perror("Error Opening /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    if (*dev)
    {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0)
    {
        perror("ioctl error TUNSETIFF");
        close(fd);
        return err;
    }

    strcpy(dev, ifr.ifr_name);

    return fd;
}

void *read_tun_packets(void *args)
{
    char buf[1024];

    char tun_name[IFNAMSIZ];
    char buffer[BUFSIZE];

    strcpy(tun_name, "mtl0");
    tun_fd = tun_alloc(tun_name, IFF_TUN | IFF_NO_PI | IFF_MULTI_QUEUE);

    while (1)
    {
        printf("Wait for TUN packets \n");
        int n = read(tun_fd, buf, sizeof(buf));
        if (n < 0)
        {
            perror("read");
            break;
        }
        else
        {
            printf("Got TUN packet \n");
        }

        // Send the data as a UDP packet to another IP address and port.
        //send_udp_packet(buf, n, "192.168.10.1", 1234);
        send_udp_packet(buf, n, target_address, PORT);
        
    }

    return NULL;
}

int setup_udp()
{

    // Create a UDP socket.
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        perror("socket");
        return 1;
    }

    // Bind the UDP socket to port def as PORT
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    return 0;
}

void setup_target_address(int n) {
    memset(target_address, 0, sizeof(target_address));
    sprintf(target_address, "192.168.10.%d", n);
}

int main(int argc, char **argv)
{
    char buf[1024];

    //Specify the target destination, our TUN server endpoiny
    int target_id = atoi(argv[1]);
    setup_target_address(target_id);

    // Setup a UDP interface for sending and receiving packets
    setup_udp();

    // Create a thread to read the incoming UDP packets.
    pthread_t udp_thread;
    void *(*udp_func)(void *) = &read_udp_packets;
    void *args = NULL;
    pthread_create(&udp_thread, NULL, udp_func, args);

    // Create a thread that sets up the TUN interface
    // and reads packets from it
    pthread_t tun_thread;
    void *(*tun_func)(void *) = &read_tun_packets;
    pthread_create(&tun_thread, NULL, tun_func, args);

    // Do other stuff...

    // Wait for the udp thread to finish.
    pthread_join(udp_thread, NULL);

    // Close the TUN device and the UDP socket.
    close(tun_fd);
    close(sock_fd);

    return 0;
}
