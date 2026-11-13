#pragma once
// Loopback stand-in for a payload: a plain UDP socket the SDK connects to. Test-only.

#include "mavlink_frame.h"

#include <QByteArray>

#include "test_socket.h"

class FakePayload
{
public:
    FakePayload()
    {
        testSocketInit();
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t l = sizeof a;
        bind(fd, (sockaddr *)&a, sizeof a);
        getsockname(fd, (sockaddr *)&a, &l);
        port = ntohs(a.sin_port);
        setRecvTimeout(fd, 3000);
    }
    ~FakePayload() { closeTestSocket(fd); }

    // Waits for the app's first datagram (its heartbeat) to learn the app's ephemeral endpoint.
    bool learnAppEndpoint()
    {
        uint8_t b[512];
        socklen_t l = sizeof app;
        return recvfrom(fd, (char *)b, sizeof b, 0, (sockaddr *)&app, &l) > 0;
    }
    void send(const QByteArray &d) { sendto(fd, d.constData(), int(d.size()), 0, (sockaddr *)&app, sizeof app); }
    void send(const mavlink_message_t &m)
    {
        uint8_t b[MAVLINK_MAX_PACKET_LEN];
        send(QByteArray((const char *)b, mavlink_msg_to_send_buffer(b, &m)));
    }
    void sendHeartbeat(uint8_t comp, uint8_t type)
    {
        mavlink_message_t m;
        mavlink_msg_heartbeat_pack_chan(1, comp, MAVLINK_COMM_0, &m, type, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
        send(m);
    }

    // Every valid frame received since the last call, without blocking.
    std::vector<MavFrame> received()
    {
        std::vector<MavFrame> out;
        uint8_t b[2048];
        int n;
        while ((n = recvNow(fd, b, sizeof b)) > 0)
            for (MavFrame &f : parseDatagram(b, size_t(n)))
                if (f.status == MavFrame::Ok) out.push_back(std::move(f));
        return out;
    }

    test_socket_t fd;
    int port;
    sockaddr_in app{};
};
