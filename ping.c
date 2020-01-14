#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <sys/signal.h>
#include <netdb.h>
#include <time.h>

#define PACKET_LENGTH 64 // send 64 bytes of data
#define PORT 2020

int trans_count, recv_count;    //packet count
long tot_time;
double rtt_min = INT32_MAX, rtt_max;    //keep track of number of echo request send
char ip_addr[20] = "Wrote by Athul P";   //ip addr in format a.b.c.d


struct icmp_echo_packet {
    struct icmphdr hdr;     //icmp header
    char data[PACKET_LENGTH - sizeof(struct icmphdr)];  //fill the data part with random data
};


void sigint_handler() {     //print the statistics when pressed ctrl+c
    printf("\n--- %s ping statistics ---\n", ip_addr);
    printf("%d packets transmitted, %d received, %3.1f %% packet loss\n", trans_count, recv_count, (float)(trans_count - recv_count)/trans_count*100.0);
    printf("rtt min/max/avg = %f/%f/%ld ms\n", rtt_min/1000, rtt_max/1000, tot_time/recv_count);
    exit(0);
}

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b; 
    unsigned int sum=0; 
    unsigned short result; 
  
    for ( sum = 0; len > 1; len -= 2 ) 
        sum += *buf++; 
    if ( len == 1 ) 
        sum += *(unsigned char*)buf; 
    sum = (sum >> 16) + (sum & 0xFFFF); 
    sum += (sum >> 16); 
    result = ~sum; 
    return result; 
} 



//Called when a packet is received
void display_stat(void *buf, int bytes, long *rtt) {
	struct iphdr *ip = buf;  
	struct icmphdr *icmp = buf + ip->ihl*4;   //extract the icmp section, ihl is the ip header length in 32 bit words
    
    tot_time += *rtt/1000;
    rtt_min = rtt_min<*rtt?rtt_min:*rtt;
    rtt_max = rtt_max<*rtt?*rtt:rtt_max;

    recv_count++;   //acknowledge the received packet
    printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%ld ms\n", bytes - 20, ip_addr,icmp->un.echo.sequence, 100, (*rtt)/1000);
}

//check if the received packet is of type ECHO_REPLY and hava the same identifier
int verify_packet(void *buf, int bytes) {
    struct iphdr *ip = buf;  
	struct icmphdr *icmp = buf + ip->ihl*4;
    return (icmp->un.echo.id == 8080 && icmp->code == 0 && icmp->type == 0);
}

void send_echo(const int *raw_socket_fd, struct sockaddr_in *target_addr) {
    struct icmp_echo_packet *packet = (struct icmp_echo_packet*)malloc(sizeof(struct icmp_echo_packet));
    struct sockaddr_in recv_addr;
    unsigned char buf[1024];    //single byte buffer to store the received data
    bzero(packet, sizeof(struct icmp_echo_packet));     //zero out the packet
    bzero(buf, sizeof(buf));    //zero out the buffer

    packet->hdr.type = ICMP_ECHO;   //set the message type
    packet->hdr.code = 0;
    packet->hdr.un.echo.id = 8080;
    packet->hdr.un.echo.sequence = ++trans_count;   //incremetn the transmitted packet count
    packet->hdr.checksum = checksum(packet, sizeof(packet));

    int recv_len = sizeof(recv_addr), bytes;
    struct timeval sendt, recvt;

    gettimeofday(&sendt, NULL);     //start the timer to measure rtt

    if(sendto(*raw_socket_fd, packet, sizeof(*packet),0,(struct sockaddr*)target_addr,sizeof(*target_addr)) == -1)
        perror("Error sending ICMP packet\n");
    int timeout = 3; //set 3 second timeout
    time_t start = time(NULL);
    do {
        bytes = recvfrom(*raw_socket_fd, buf, sizeof(buf), 0, (struct sockaddr*)&recv_addr, &recv_len);
        if(time(NULL) - start > timeout) return;
    }while(!verify_packet(buf, bytes));

    if(bytes == 0) perror("packet not received\n");
    gettimeofday(&recvt, NULL);     //stop the timer and calculate rtt

    free(packet);
    long rtt = (recvt.tv_sec - sendt.tv_sec)*1000000 + (recvt.tv_usec - sendt.tv_usec);
    display_stat(buf, bytes, &rtt);
}

int main(int argc, char **argv) {
    if(argc <= 1) {
        printf("Usage: ping <ip_addr>");
        exit(1);
    }else {
        strcpy(ip_addr, argv[1]);
    }
    in_addr_t res = inet_addr(argv[1]);
    if(res == INADDR_NONE) {
        struct hostent *tmp = gethostbyname(argv[1]);
        if(tmp == NULL) {
            perror("Host not found\n");
            return 1;
        }
        res = *(long*)tmp->h_addr_list[0];
    }
    int ttl = 100;  //time to live for the icmp packet
    struct timeval recv_tout;   //set the receive timeout to  be 1 seconds
    recv_tout.tv_sec = 2;
    recv_tout.tv_usec = 0;
    signal(SIGINT, sigint_handler);
    const int raw_socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if(raw_socket_fd <= 0 ) perror("Error creating socket");
    struct sockaddr_in target_addr;
    setsockopt(raw_socket_fd, SOL_IP, IP_TTL, &ttl, sizeof(ttl));   //set the ttl
    setsockopt(raw_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_tout, sizeof(recv_tout));  //set the receive timeout

    target_addr.sin_family = AF_INET;
    target_addr.sin_port = PORT;
    target_addr.sin_addr.s_addr = res;
    printf("PING %s with %d bytes of data\n", ip_addr, PACKET_LENGTH);
    while(1) {
        send_echo(&raw_socket_fd, &target_addr);
        sleep(1);   
    }

    return 0;
}

