// Copyright 2012 Google Inc. / Pascal Gauthier - Apache License 2.0 (simplifie pour FM-9)
#include "tuning.h"

struct StandardTuning : public TuningState {
    StandardTuning() {
        const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
        const int step = (1 << 24) / 12;
        for( int mn = 0; mn < 128; ++mn )
            current_logfreq_table_[mn] = base + step * mn;
    }
    int32_t midinote_to_logfreq(int midinote) override {
        return current_logfreq_table_[midinote < 0 ? 0 : (midinote > 127 ? 127 : midinote)];
    }
    int current_logfreq_table_[128];
};

std::shared_ptr<TuningState> createStandardTuning() { return std::make_shared<StandardTuning>(); }
