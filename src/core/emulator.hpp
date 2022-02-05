#pragma once
#include <fstream>
#include <functional>

#include "ee/dmac.hpp"
#include "ee/emotion.hpp"
#include "ee/intc.hpp"
#include "ee/ipu/ipu.hpp"
#include "ee/timers.hpp"
#include "ee/vu/vif.hpp"
#include "ee/vu/vu.hpp"

#include "iop/cdvd/cdvd.hpp"
#include "iop/sio2/gamepad.hpp"
#include "iop/iop.hpp"
#include "iop/iop_dma.hpp"
#include "iop/iop_intc.hpp"
#include "iop/iop_timers.hpp"
#include "iop/sio2/memcard.hpp"
#include "iop/sio2/sio2.hpp"
#include "iop/spu/spu.hpp"
#include "iop/sio2/firewire.hpp"

#include <util/int128.hpp>
#include <gs/gs.hpp>
#include <gs/gif.hpp>
#include <sif.hpp>
#include "scheduler.hpp"

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
    private:
        std::atomic_bool save_requested, load_requested, gsdump_requested, gsdump_single_frame, gsdump_running;
        std::string save_state_path;
        int frames;
        ee::Cop0 cp0;
        ee::Cop1 fpu;
        cdvd::CDVD_Drive cdvd;
        ee::DMAC dmac;
        ee::EmotionEngine cpu;
        ee::EmotionTiming timers;
        sio2::Firewire firewire;
        sio2::Gamepad pad;
        gs::GraphicsSynthesizer gs;
        gs::GraphicsInterface gif;
        iop::IOP iop;
        iop::IOP_DMA iop_dma;
        iop::IOPTiming iop_timers;
        ee::INTC intc;
        ipu::ImageProcessingUnit ipu;
        sio2::Memcard memcard;
        Scheduler scheduler;
        sio2::SIO2 sio2;
        spu::SPU spu, spu2;
        SubsystemInterface sif;
        vu::VectorInterface vif0, vif1;
        vu::VectorUnit vu0, vu1;

        int vblank_start_id, vblank_end_id, spu_event_id, hblank_event_id, gs_vblank_event_id;

        bool VBLANK_sent;
        bool cop2_interlock, vu_interlock;

        std::ofstream ee_log;
        std::string ee_stdout;

        uint8_t* RDRAM;
        uint8_t* IOP_RAM;
        uint8_t* BIOS;
        uint8_t* SPU_RAM;

        uint8_t scratchpad[1024 * 16];
        uint8_t iop_scratchpad[1024];

        uint32_t iop_scratchpad_start;

        uint32_t MCH_RICM, MCH_DRD;
        uint8_t rdram_sdevid;

        uint8_t IOP_POST;

        iop::IOP_INTC iop_intc;

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
        gs::GraphicsSynthesizer& get_gs();//used for gs dumps

        void set_wav_output(bool state);
    };
}