#pragma once
#include <fstream>
#include <functional>
#include <memory>
#include <util/int128.hpp>
#include <iop/sio2/gamepad.hpp>
#include <iop/cdvd/cdvd.hpp>

namespace core
{
    class Scheduler;
    class SubsystemInterface;
}

namespace ee
{
    class EmotionEngine;
    class EmotionTiming;
    class INTC;
    class DMAC;
}

namespace iop
{
    class IOP;
    class IOPTiming;
    class INTC;
    class DMA;
}

namespace sio2
{
    class SIO2;
    class Firewire;
    class Memcard;
    class Gamepad;
}

namespace gs
{
    class GraphicsSynthesizer;
    class GraphicsInterface;
}

namespace ipu
{
    class ImageProcessingUnit;
}

namespace vu
{
    class VectorInterface;
    class VectorUnit;
}

namespace spu
{
    class SPU;
}

namespace cdvd
{
    class CDVD_Drive;
}

namespace core
{
    /* NTSC Interlaced Timings */
    /* 4920115.2 EE cycles to be exact FPS of 59.94005994005994hz */
    constexpr uint32_t CYCLES_PER_FRAME = 4920115;
    /* 4489019.391883126 Guess, exactly 23 HBLANK's before the end */
    constexpr uint32_t VBLANK_START_CYCLES = 4489019;
    constexpr uint32_t HBLANK_CYCLES = 18742;
    /* CSR FIELD swap/vblank happens ~65622 cycles after the INTC VBLANK_START event */
    constexpr uint32_t GS_VBLANK_DELAY = 65622;

    /* These constants are used for the fast boot hack for.isos */
    constexpr uint32_t EELOAD_START = 0x82000;
    constexpr uint32_t EELOAD_SIZE = 0x20000;

    enum SKIP_HACK
    {
        NONE,
        LOAD_ELF,
        LOAD_DISC
    };

    enum CPU_MODE
    {
        DONT_CARE,
        JIT,
        INTERPRETER
    };

    class Emulator
    {
    public:
        std::atomic_bool save_requested, load_requested, gsdump_requested, gsdump_single_frame, gsdump_running;
        std::string save_state_path;
        int frames;
        
        /* Emulation components */
        std::unique_ptr<cdvd::CDVD_Drive> cdvd;
        std::unique_ptr<ee::DMAC> dmac;
        std::unique_ptr<ee::EmotionEngine> cpu;
        std::unique_ptr<ee::EmotionTiming> timers;
        std::unique_ptr<ee::INTC> intc;
        std::unique_ptr<sio2::Firewire> firewire;
        std::unique_ptr<sio2::Gamepad> pad;
        std::unique_ptr<gs::GraphicsSynthesizer> gs;
        std::unique_ptr<gs::GraphicsInterface> gif;
        std::unique_ptr<iop::IOP> iop;
        std::unique_ptr<iop::DMA> iop_dma;
        std::unique_ptr<iop::IOPTiming> iop_timers;
        std::unique_ptr<iop::INTC> iop_intc;
        std::unique_ptr<ipu::ImageProcessingUnit> ipu;
        std::unique_ptr<sio2::Memcard> memcard;
        std::unique_ptr<Scheduler> scheduler;
        std::unique_ptr<sio2::SIO2> sio2;
        std::unique_ptr<spu::SPU> spu, spu2;
        std::unique_ptr<SubsystemInterface> sif;
        std::unique_ptr<vu::VectorInterface> vif0, vif1;
        std::unique_ptr<vu::VectorUnit> vu0, vu1;

        int vblank_start_id, vblank_end_id, spu_event_id, hblank_event_id, gs_vblank_event_id;

        bool VBLANK_sent;
        bool cop2_interlock, vu_interlock;

        std::ofstream ee_log;
        std::string ee_stdout;

        uint8_t* BIOS;
        uint8_t* SPU_RAM;

        uint32_t MCH_RICM, MCH_DRD;
        uint8_t rdram_sdevid;

        uint8_t IOP_POST;

        SKIP_HACK skip_BIOS_hack;

        uint8_t* ELF_file;
        uint32_t ELF_size;

        void iop_IRQ_check(uint32_t new_stat, uint32_t new_mask);
        void start_sound_sample_event();

        bool frame_ended;
    public:
        Emulator();
        ~Emulator();
        void run();
        void reset();
        void print_state();
        void press_button(sio2::PAD_BUTTON button);
        void release_button(sio2::PAD_BUTTON button);
        void update_joystick(sio2::JOYSTICK joystick, sio2::JOYSTICK_AXIS axis, uint8_t val);
        bool skip_BIOS();
        void fast_boot();
        void set_skip_BIOS_hack(SKIP_HACK type);
        void set_ee_mode(CPU_MODE mode);
        void set_vu0_mode(CPU_MODE mode);
        void set_vu1_mode(CPU_MODE mode);
        void load_BIOS(const uint8_t* BIOS);
        void load_ELF(const uint8_t* ELF, uint32_t size);
        bool load_CDVD(const char* name, cdvd::CDVD_CONTAINER type);
        void load_memcard(int port, const char* name);
        std::string get_serial();
        void execute_ELF();
        uint32_t* get_framebuffer();
        void get_resolution(int& w, int& h);
        void get_inner_resolution(int& w, int& h);

        //Events
        void hblank_event();
        void GS_vblank_event();
        void vblank_start();
        void vblank_end();
        void cdvd_event();
        void gen_sound_sample();

        bool request_load_state(const char* file_name);
        bool request_save_state(const char* file_name);
        void request_gsdump_toggle();
        void request_gsdump_single_frame();
        void load_state(const char* file_name);
        void save_state(const char* file_name);

        bool interlock_cop2_check(bool isCOP2);
        void clear_cop2_interlock();
        bool check_cop2_interlock();

        uint8_t read8(uint32_t address);
        uint16_t read16(uint32_t address);
        uint32_t read32(uint32_t address);
        uint64_t read64(uint32_t address);
        uint128_t read128(uint32_t address);
        void write8(uint32_t address, uint8_t value);
        void write16(uint32_t address, uint16_t value);
        void write32(uint32_t address, uint32_t value);
        void write64(uint32_t address, uint64_t value);
        void write128(uint32_t address, uint128_t value);

        void ee_kputs(uint32_t param);
        void ee_deci2send(uint32_t addr, int len);

        uint8_t iop_read8(uint32_t address);
        uint16_t iop_read16(uint32_t address);
        uint32_t iop_read32(uint32_t address);
        void iop_write8(uint32_t address, uint8_t value);
        void iop_write16(uint32_t address, uint16_t value);
        void iop_write32(uint32_t address, uint32_t value);

        void iop_ksprintf();
        void iop_puts();

        void test_iop();
        gs::GraphicsSynthesizer* get_gs();//used for gs dumps

        void set_wav_output(bool state);
    };
}