#pragma once

#include <meta>
#include <vector>
#include "name_spec.hpp"

consteval static bool unique_spec_name(std::vector<std::meta::info> input)
{
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        auto spec_i = std::meta::extract<name_spec>(input[i]);
        for (std::size_t j = i + 1; j < input.size(); ++j)
        {
            auto spec_j = std::meta::extract<name_spec>(input[j]);
            if (spec_i.name == spec_j.name)
                return false;
        }
    }
    return true;
}