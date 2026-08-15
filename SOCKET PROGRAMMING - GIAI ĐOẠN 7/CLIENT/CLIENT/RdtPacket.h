#pragma once
#include "lib.h"



constexpr uint8_t FLAG_DATA = 0x01;
constexpr uint8_t FLAG_ACK = 0x02;
constexpr uint8_t FLAG_FIN = 0x04;

constexpr int RDT_TIMEOUT_MS = 500;
constexpr int RDT_POLL_MS = 50;
constexpr int RDT_MAX_RETRIES = 20;
constexpr int RDT_MAX_PAYLOAD = 1024;
constexpr int RDT_HEADER_SIZE = 9;

constexpr int RDT_INITIAL_WINDOW = 4;
constexpr int RDT_MIN_WINDOW = 1;
constexpr int RDT_MAX_WINDOW = 32;

constexpr bool SIMULATE_PACKET_LOSS = false;
constexpr int  LOSS_PERCENT         = 10;


struct RdtPacket {
    uint32_t seqNum;
    uint8_t  flags;
    uint16_t checksum;
    uint16_t payloadLength;
    vector<char> payload;
};

uint16_t computeChecksum(const char*, int);

bool verifyChecksum(const char*, int);

vector<char> serializePacket(const RdtPacket&);

bool deserializePacket(const char*, int, RdtPacket&);

bool shouldSimulateLoss();
