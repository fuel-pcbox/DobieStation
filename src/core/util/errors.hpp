#pragma once
#include <stdexcept>

constexpr uint32_t ERROR_STRING_MAX_LENGTH = 255;

class Errors
{
    public:
        [[ noreturn ]] static void die(const char* format, ...);
        static void non_fatal(const char* format, ...);
        static void print_warning(const char* format, ...);//ignores the error, just print it.
};

class Emulation_error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class non_fatal_error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};