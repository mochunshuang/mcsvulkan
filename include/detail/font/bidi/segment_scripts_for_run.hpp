#pragma once

#include "bidi_script.hpp"
#include <cassert>
#include <span>
#include <vector>

#include "../../utils/unique_handle.hpp"
#include "sb_script_to_hb_script.hpp"

namespace mcs::vulkan::font::bidi
{
    constexpr static auto segment_scripts_for_run(std::span<const uint32_t> codepoints)
        -> std::vector<bidi_script>
    {
        using SBScriptLocatorPtr =
            unique_handle<SBScriptLocatorRef,
                          [](SBScriptLocatorRef value) constexpr noexcept {
                              SBScriptLocatorRelease(value);
                          }>;

        SBCodepointSequence sequence{.stringEncoding = SBStringEncodingUTF32,
                                     .stringBuffer = codepoints.data(),
                                     .stringLength = codepoints.size()};

        SBScriptLocatorPtr RAIIlocator = SBScriptLocatorPtr{SBScriptLocatorCreate()};
        auto *locator = RAIIlocator.get();
        const SBScriptAgent *agent = SBScriptLocatorGetAgent(locator);

        SBScriptLocatorLoadCodepoints(locator, &sequence);

        std::vector<bidi_script> output;
        output.reserve(1);
        while (SBScriptLocatorMoveNext(locator) != 0)
            output.emplace_back(
                bidi_script{.offset = agent->offset,
                            .length = agent->length,
                            .script = sb_script_to_hb_script(agent->script)});
        return output;
    }
}; // namespace mcs::vulkan::font::bidi