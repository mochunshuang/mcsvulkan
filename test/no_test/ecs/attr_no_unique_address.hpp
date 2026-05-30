#pragma once

#if __has_cpp_attribute(msvc::no_unique_address)
#define ATTR_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define ATTR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
