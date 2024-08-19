#include <iostream>
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <thread>
#include <vector>
#include <string>

// Link with Iphlpapi.lib and ws2_32.lib
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#define PING_TIMEOUT 5000

const char MainPorts[] = "20, 21, 22, 23, 25, 53, 69, 80, 110, 119, 123, 135, 137, 139, 143, 161, 443, 445, 636, 691, 3389, 5900";

void removeSpaces(char* s) {
    *std::remove(s, s + strlen(s), ' ') = 0;
}

std::vector<int> explodePList(char* port_list) {
    std::vector<int> res;
    size_t list_size = strlen(port_list) + 1;
    char* list_container = new char[list_size];
    strcpy_s(list_container, list_size, port_list);
    std::string s(list_container);
    int newpos = 0;
    while (s.find(",") != std::string::npos) {
        std::string r = s.substr(0, s.find(","));
        res.push_back(stoi(r));
        s = s.substr(s.find(",") + 1);
    }
    res.push_back(stoi(s));
    return res;
}

void ListIpAddresses() {
    IP_ADAPTER_ADDRESSES* adapter_addresses(NULL);
    IP_ADAPTER_ADDRESSES* adapter(NULL);
    DWORD adapter_addresses_buffer_size = 16 * 1024;
    // Get adapter addresses
    for (int attempts = 0; attempts != 3; ++attempts) {
        adapter_addresses = (IP_ADAPTER_ADDRESSES*)malloc(adapter_addresses_buffer_size);
        DWORD error = ::GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER |
            GAA_FLAG_SKIP_FRIENDLY_NAME,
            NULL,
            adapter_addresses,
            &adapter_addresses_buffer_size);
        if (ERROR_SUCCESS == error) {
            break;
        }
        else if (ERROR_BUFFER_OVERFLOW == error) {
            // Try again with the new size
            free(adapter_addresses);
            adapter_addresses = NULL;
            continue;
        }
        else {
            // Unexpected error code - log and throw
            free(adapter_addresses);
            adapter_addresses = NULL;
            return;
        }
    }
    // Iterate through all of the adapters
    for (adapter = adapter_addresses; NULL != adapter; adapter = adapter->Next) {
        // Skip loopback adapters
        if (IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType) continue;
        printf("[ADAPTER]: %S\n", adapter->Description);
        printf("[NAME]:    %S\n", adapter->FriendlyName);
        // Parse all IPv4 addresses
        for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress; NULL != address; address = address->Next) {
            auto family = address->Address.lpSockaddr->sa_family;
            if (AF_INET == family) {
                SOCKADDR_IN* ipv4 = reinterpret_cast<SOCKADDR_IN*>(address->Address.lpSockaddr);
                char str_buffer[16] = { 0 };
                inet_ntop(AF_INET, &(ipv4->sin_addr), str_buffer, 16);
                printf("[IP]:      %s\n", str_buffer);
            }
        }
        printf("\n");
    }
    free(adapter_addresses);
    adapter_addresses = NULL;
}

void GetOnline(const char* ip) {
    ULONG status;
    HANDLE hIcmpFile;
    IPAddr addr;
    char SendData[32] = "Data Buffer";
    LPVOID ReplyBuffer = NULL;
    DWORD ReplySize = 0;
    DWORD dwRetVal = 0;
    hIcmpFile = IcmpCreateFile();
    if (hIcmpFile == INVALID_HANDLE_VALUE) {
        printf("\tUnable to open handle.\n");
    }
    inet_pton(AF_INET, ip, &addr);
    ReplySize = sizeof(ICMP_ECHO_REPLY) + sizeof(SendData);
    ReplyBuffer = (VOID*)malloc(ReplySize);
    dwRetVal = IcmpSendEcho(hIcmpFile, addr, SendData, sizeof(SendData),
        NULL, ReplyBuffer, ReplySize, PING_TIMEOUT);
    if (dwRetVal != 0) {
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
        struct in_addr ReplyAddr;
        ReplyAddr.S_un.S_addr = pEchoReply->Address;
        status = pEchoReply->Status;
        free(ReplyBuffer);
        if (status == 0) {
            printf("%s is online \n", ip);
        }
    }
    else {
        free(ReplyBuffer);
    }
}

void GetOnlineNet(const char* mask) {
    std::vector<std::thread> threads;
    size_t msize = strlen(mask);
    for (int i = 0; i < 256; i++) {
        char z[4];
        _itoa_s(i, z, 4, 10);
        size_t zsize = strlen(z);
        size_t nsize = msize + zsize + 1;
        char* pmask = new char[nsize];
        strcpy_s(pmask, nsize, mask);
        strcpy_s(pmask + msize, nsize, ".");
        strcpy_s(pmask + msize + 1, nsize, z);
        //GetOnline(pmask);
        threads.push_back(std::thread(GetOnline, pmask));
    }
    for (auto& th : threads) {
        th.join();
    }
}

void PortScan(const char* ip, const int port) {
    DWORD timeout = 8000;
    WSADATA data;
    SOCKET sock;
    IN_ADDR addr;
    sockaddr_in sock_addr;
    int status;
    char recbuffer[256];
    if ((WSAStartup(MAKEWORD(2, 0), &data) != 0))
    {
        printf("Error: Winsock did not init!!!\n\n");
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    sock_addr.sin_family = PF_INET;
    sock_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr);
    sock_addr.sin_addr = addr;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    status = connect(sock, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
    int err = WSAGetLastError();
    if (err == 0) {
        printf("Port %i is open \n", port);
    }
    //printf("Port %i: %i \n", i, err);
    closesocket(sock);
    WSACleanup();
}

void MTPortScan(const char* ip, std::vector<int> ports) {
    std::vector<std::thread> threads;
    int asize = ports.size();
    for (int i = 0; i < asize; i++) {
        threads.push_back(std::thread(PortScan, ip, ports[i]));
    }
    for (auto& th : threads) {
        th.join();
    }
}


int main(int argc, char* argv[]) {
    printf("Loaua network discovery tool \n\n");
    bool error_arg = 0;
    if (argc < 2) {
        printf("See help (-h) for command list. \n");
        return 0;
    }
    if (std::strcmp(argv[1], "-h") == 0) {
        printf("Usage: \n");
        printf("  -l                 list all adapters and IP4 addresses \n");
        printf("  -n  <net mask>     scan the network for online hosts, ex. -n 192.168.0 \n");
        printf("  -p  <ip> <ports>   scan the given IP for given ports, ex. -p 192.168.0.1 22,25 \n");
        printf("  -mp <ip>           scan the given IP for main used network ports \n");
        return 0;
    }
    if (std::strcmp(argv[1], "-l") == 0) {
        ListIpAddresses();
        return 0;
    }
    if (std::strcmp(argv[1], "-n") == 0) {
        GetOnlineNet(argv[2]);
        return 0;
    }
    if (std::strcmp(argv[1], "-p") == 0){
        char port_arr[MAX_PATH];
        strcpy_s(port_arr, 260, argv[3]);
        removeSpaces(port_arr);
        std::vector<int> p = explodePList(port_arr);
        MTPortScan(argv[2], p);
        return 0;
    }
    if (std::strcmp(argv[1], "-mp") == 0){
        char port_arr[MAX_PATH];
        strcpy_s(port_arr, 260, MainPorts);
        removeSpaces(port_arr);
        std::vector<int> p = explodePList(port_arr);
        MTPortScan(argv[2], p);
        return 0;
    }
    printf("Something wrong. See help (-h) for command list. \n");
    return 0;
}
