#pragma once

namespace tinydraw::esp32 {

class VectorV2Presenter;
struct LivePresentationTiming;

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing);

}  // namespace tinydraw::esp32
