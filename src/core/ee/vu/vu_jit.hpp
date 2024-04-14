#pragma once
#include <cstdint>

namespace vu
{
	class VectorUnit;

	namespace jit
	{
		uint16_t run(VectorUnit* vu);
		void reset(VectorUnit* vu);
		void set_current_program(uint32_t crc, VectorUnit* vu);
	}
};
