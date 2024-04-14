#pragma once
#include <cstdint>
#include <fstream>
#include <queue>

namespace iop
{
    class INTC;
}

namespace sio2
{
    enum class SIO_DEVICE
    {
        NONE,
        PAD,
        MEMCARD,
        DUMMY
    };

    class Gamepad;
    class Memcard;

    class SIO2
    {
    private:
        iop::INTC* intc;
        Gamepad* pad;
        Memcard* memcard;

        uint32_t send1[4];
        uint32_t send2[4];
        uint32_t send3[16];

        uint32_t RECV1;
        uint32_t RECV3;

        int port;
        int dma_bytes_received;

        std::queue<uint8_t> FIFO;
        uint32_t control;

        bool new_command;
        SIO_DEVICE active_command;
        int command_length;
        int send3_port;

        void write_device(uint8_t value);
    
    public:
        SIO2(iop::INTC* intc, Gamepad* pad, Memcard* memcard);

        void reset();

        uint8_t read_serial();
        uint32_t get_control();
        uint32_t get_RECV1();
        uint32_t get_RECV2();
        uint32_t get_RECV3();

        void set_send1(int index, uint32_t value);
        void set_send2(int index, uint32_t value);
        void set_send3(int index, uint32_t value);

        void set_control(uint32_t value);
        void write_serial(uint8_t value);
        void write_dma(uint8_t value);
        void dma_reset();
    };
}