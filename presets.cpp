void loadPresets(VoiceGeneratorRepository& voiceRepo) {
    voiceRepo.addVoiceGenerator("FM Voice", [](float frequency, float volume) {
        return std::make_shared<ADSRGenerator>(std::make_shared<FMVoice>(frequency, frequency / 2.111f, 0.75f));
    });

    voiceRepo.addVoiceGenerator("Bell", [](float frequency, float volume) {
        auto fmVoice = std::make_shared<FMVoice>(frequency, frequency * 1.22f, 0.82f, 0.3f);
        auto tremolo = std::make_shared<Tremolo>(fmVoice, 1.7f, 0.13f);
        return std::make_shared<ADSRGenerator>(
            tremolo,
            0.01f,  // Attack: Very short for a bell-like sound
            2.0f,   // Decay: Quick decay
            0.0f,   // Sustain: Lower sustain level for bell-like sound
            2.0f    // Release: Longer release for bell-like decay
        );
    });

    voiceRepo.addVoiceGenerator("Harmonic Tone", [](float frequency, float volume) {
        return std::make_shared<ADSRGenerator>(std::make_shared<HarmonicTone>(frequency, volume));
    });

    voiceRepo.addVoiceGenerator("Sine Oscillator", [](float frequency, float volume) {
        auto adsr = std::make_shared<ADSRGenerator>(
            std::make_shared<Oscillator>(frequency, volume),
            0.05f,  // Attack
            0.1f,   // Decay
            0.7f,   // Sustain
            0.3f    // Release
        );
        return adsr;
    });

    voiceRepo.addVoiceGenerator("Saw Oscillator", [](float frequency, float volume) {
        auto oscillator = std::make_shared<Oscillator>(frequency, volume, Waveform::Sawtooth);
        auto tremolo = std::make_shared<Tremolo>(oscillator, 5.0f, 0.3f);
        auto adsr = std::make_shared<ADSRGenerator>(
            tremolo,
            0.05f,  // Attack
            0.1f,   // Decay
            0.7f,   // Sustain
            0.3f    // Release
        );
        auto delay = std::make_shared<InterpolatedDelay>(adsr, 0.3f * 44100, 0.5f, 0.3f, 44100);
        return delay;
    });

    voiceRepo.addVoiceGenerator("Bass", [](float frequency, float volume) {
        auto fmVoice = std::make_shared<FMVoice>(
            frequency,
            frequency * 0.36f,  // Modulator Frequency Ratio: 0.36
            0.78f,              // Modulation Index: 0.78
            0.7f                // Self Modulation Index: 0.7
        );
        return std::make_shared<ADSRGenerator>(
            fmVoice,
            0.01f,  // Attack: 0.01s
            0.4f,   // Decay: 0.4s
            0.0f,   // Sustain: 0
            0.39f   // Release: 0.39s
        );
    });

    voiceRepo.addVoiceGenerator("Trio", [](float frequency, float volume) {
        const std::string mainSuffix = "(main)";
        const std::string harmonicSuffix = "(harmonic)";
        const std::string resonanceSuffix = "(resonance)";

        // Main voice - fundamental tone with bright attack
        auto fm0 = std::make_shared<FMVoice>(
            frequency,
            frequency * 2.0f,    // Increased modulator frequency for brighter attack
            0.3f,               // More pronounced modulation
            0.1f                // Slight self modulation for complexity
        );
        auto adsr0 = std::make_shared<ADSRGenerator>(
            fm0,
            0.001f,  // Nearly instantaneous attack
            0.8f,    // Faster initial decay
            0.2f,    // Slight sustain for longer notes
            0.6f     // Natural release
        );
        fm0->addSuffix(mainSuffix);
        adsr0->addSuffix(mainSuffix);

        // String harmonics simulation
        auto fm1 = std::make_shared<HarmonicTone>(frequency * 1.001f, volume); // Slight detuning
        auto adsr1 = std::make_shared<ADSRGenerator>(
            fm1,
            0.001f,  // Immediate attack
            1.2f,    // Longer decay for harmonics
            0.1f,    // Very slight sustain
            0.8f     // Longer release for natural decay
        );
        fm1->addSuffix(harmonicSuffix);
        adsr1->addSuffix(harmonicSuffix);

        // Sympathetic string resonance
        auto fm2 = std::make_shared<FMVoice>(
            frequency * 0.5f,    // Lower octave for body resonance
            frequency * 0.499f,  // Slight detuning for movement
            0.2f,               // Moderate modulation
            0.15f               // Increased self-modulation for complexity
        );
        auto adsr2 = std::make_shared<ADSRGenerator>(
            fm2,
            0.002f,  // Slightly delayed attack
            2.0f,    // Long decay for resonance
            0.05f,   // Minimal sustain
            1.2f     // Long release for natural resonance
        );
        fm2->addSuffix(resonanceSuffix);
        adsr2->addSuffix(resonanceSuffix);

        std::vector<std::shared_ptr<SoundGenerator>> sources = {adsr0, adsr1, adsr2};
        
        // Create mixer with adjusted volumes
        std::vector<std::string> suffixes = {mainSuffix, harmonicSuffix, resonanceSuffix};
        auto mixer = std::make_shared<Mixer>(sources, suffixes);
        mixer->getVolumeParam(0)->setValue(0.6f);    // Strong initial strike
        mixer->getVolumeParam(1)->setValue(0.25f);   // More pronounced harmonics
        mixer->getVolumeParam(2)->setValue(0.15f);   // Subtle but present resonance
        return mixer;
    });
}
