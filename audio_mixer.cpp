//
// Audio mixer function from my game. Called every frame to write and advance the currently playing sounds.
// The sound output has 2 channels (left and right). Supports linearly changing the pitch, panning and volume of individual sounds. 
//
// Has a bit of unincluded context.
//

void MixerOutputSound(audio_state *state, game_sound_output_buffer *outBuffer, 
                      void *tempMem, s32 tempMemSize){
    // TODO: if this takes too long, consider storing sounds as floats. It'll double the
    // memory but free us from millions of s16->f32 conversions per second.

    // "Extra" samples are samples that we write in case the next frame lags too much,
    // but if everything goes well we'll overwrite them at the next step.

    s32 maxSamplesToWriteWithoutExtra = outBuffer->samplesToWrite;
    // We write an arbitrary number of extra samples.
    // That's gona help us recover when game updates take too long.
    s32 maxSamplesToWrite = MinS32(maxSamplesToWriteWithoutExtra + (outBuffer->samplesPerSecond/10),
                                   outBuffer->bufferSize*(s32)sizeof(s16));

    f32 *sumBuffer = (f32 *)tempMem;
    s32 sumBufSize = maxSamplesToWrite*sizeof(f32)*2;
    memset((void *)sumBuffer, 0, sumBufSize);

    Assert(sumBufSize <= tempMemSize);
    
    playing_sound **prevPtr = &state->firstPlayingSound;
    playing_sound *s        = state->firstPlayingSound;

    while(s){
        s->startedPlaying = true;

        f32 *sumScan = sumBuffer;
        s32 samplesToWrite = maxSamplesToWrite;
        s32 samplesToWriteWithoutExtra = maxSamplesToWriteWithoutExtra;

        // Starts being the index of the first sample from the source that will be copied
        // to the sum buffer.
        s32 curSample = s->currentSample; 

        // Sound delay
        if (s->currentSample < 0){
            s32 newCurrentSampleWithoutExtra = s->currentSample + samplesToWriteWithoutExtra;

            if (newCurrentSampleWithoutExtra < 0){
                s32 newCurrentSample = s->currentSample + samplesToWrite;
                if (newCurrentSample < 0){
                    // There is only delay, no samples.
                    s->currentSample = newCurrentSampleWithoutExtra;
                    
                    prevPtr = &s->next;
                    s       = s->next;
                    continue;
                }else{
                    // There is some delay, then extra samples.
                    samplesToWrite += s->currentSample;
                    samplesToWriteWithoutExtra = 0;
                    s->currentSample = newCurrentSampleWithoutExtra;
                    curSample = 0;
                    sumScan += 2*(-s->currentSample);
                }
            }else{
                // There is some delay, then some non-extra samples, then extra samples.
                samplesToWrite += s->currentSample; // subtract the delay frames.
                samplesToWriteWithoutExtra += s->currentSample;
                s->currentSample = 0;
                curSample = 0;
                sumScan += 2*(-s->currentSample);
            }
        }
        Assert(curSample >= 0);
        
        loaded_sound *loadedSound = SoundIdGetSound(state, s->loadedSoundId);
        s32 srcNumChannels = (s32)loadedSound->numChannels;
        s32 srcIsStereo    = (srcNumChannels == 2 ? 1 : 0);
        s32 srcNumSamples  = (s32)loadedSound->numSamples;
        s16 *srcScan       = loadedSound->mem + (curSample*srcNumChannels);
        


        if (s->pitchTarget == s->pitch && s->pitch == 1.0f && 
            s->volumeTarget[0] == s->volume[0] && s->volumeTarget[1] == s->volume[1])
        {
            if (!s->loop){
// Constant normal pitch, Constant volume, No loop
                int soundSamplesToWrite = MinS32(samplesToWrite,
                                                 (s32)loadedSound->numSamples - curSample);
                f32 vol[2] = {s->volume[0], s->volume[1]};
                if (srcIsStereo){
                    for(s32 i = 0; i < soundSamplesToWrite; i++){
                        sumScan[0] += srcScan[0]*vol[0]; // L
                        sumScan[1] += srcScan[1]*vol[1]; // R
                        sumScan += 2;
                        srcScan += 2;
                    }
                }else{
                    for(s32 i = 0; i < soundSamplesToWrite; i++){
                        f32 sample = (f32)*srcScan++;
                        sumScan[0] += sample*vol[0]; // L
                        sumScan[1] += sample*vol[1]; // R
                        sumScan += 2;
                    }
                }
                s->currentSample += samplesToWriteWithoutExtra;

                if (s->currentSample >= srcNumSamples){
                    goto LABEL_FinishSound;
                }
            }else{
// Constant normal pitch, Constant volume, Loop
                for(s32 written = 0; written < samplesToWrite;){
                    s32 soundSamplesToWrite = MinS32(samplesToWrite - written, srcNumSamples - curSample);
                    for(s32 i = 0; i < soundSamplesToWrite; i++){
                        *sumScan++ += ((f32)*srcScan)*s->volume[0]; // L
                        srcScan += srcIsStereo;
                        *sumScan++ += ((f32)*srcScan++)*s->volume[1]; // R
                    }
                    written += soundSamplesToWrite;
                    curSample += soundSamplesToWrite;
                    if (curSample >= srcNumSamples){
                        curSample = 0;
                        srcScan = loadedSound->mem;
                    }
                }

                s->currentSample = (s->currentSample + samplesToWriteWithoutExtra) % srcNumSamples;
            }
        }else if (s->pitchTarget == s->pitch && s->pitch == 1.0f){
// Constant normal pitch, Modulated volume (loop & no loop)
            f32 vol[2] = {s->volume[0], s->volume[1]}; // extra samples volume
            f32 dVolume[2] = {s->dVolume[0], s->dVolume[1]};
            f32 volumeTarget[2] = {s->volumeTarget[0], s->volumeTarget[1]};

            for(s32 written = 0; written < samplesToWrite;){ // This for is only used if loop.
                s32 soundSamplesToWrite = MinS32(samplesToWrite - written, srcNumSamples - curSample);
                s32 soundSamplesToWriteWithoutExtra = ClampS32(samplesToWriteWithoutExtra - written,
                                                               0, srcNumSamples - curSample);
                // Loop advancing the actual volume
                for(int i = 0; i < soundSamplesToWriteWithoutExtra; i++){
                    *sumScan++ += ((f32)*srcScan)*s->volume[0]; // L
                    srcScan += srcIsStereo;
                    *sumScan++ += ((f32)*srcScan++)*s->volume[1]; // R
                    
                    s->volume[0] = MoveValueTo(s->volume[0], volumeTarget[0], dVolume[0]);
                    s->volume[1] = MoveValueTo(s->volume[1], volumeTarget[1], dVolume[1]);
                }
                if (soundSamplesToWriteWithoutExtra){
                    vol[0] = s->volume[0];
                    vol[1] = s->volume[1];
                }
                // Loop the extra samples without changing the actual volume
                // (if the non-extra samples haven't reached the end of the sound already)
                for(int i = soundSamplesToWriteWithoutExtra; i < soundSamplesToWrite; i++){
                    *sumScan++ += ((f32)*srcScan)*vol[0]; // L
                    srcScan += srcIsStereo;
                    *sumScan++ += ((f32)*srcScan++)*vol[1]; // R
                        
                    vol[0] = MoveValueTo(vol[0], volumeTarget[0], dVolume[0]);
                    vol[1] = MoveValueTo(vol[1], volumeTarget[1], dVolume[1]);
                }
                s->currentSample += soundSamplesToWriteWithoutExtra;

                if (s->loop){
                    written += soundSamplesToWrite;
                    curSample += soundSamplesToWrite;
                    if (curSample >= srcNumSamples){
                        curSample = 0;
                        srcScan = loadedSound->mem;
                        if (s->currentSample >= srcNumSamples)
                            s->currentSample = 0;
                    }
                }else{
                    if (s->currentSample >= srcNumSamples){
                        goto LABEL_FinishSound;
                    }
                    break;
                }
            }
        }else if (s->pitch == s->pitchTarget && s->volume[0] == s->volumeTarget[0] && 
                  s->volume[1] == s->volumeTarget[1])
        {
// Constant custom pitch, Constant volume (loop & no loop)
            
            f32 pitch = s->pitch;
            AssertRange(.00001f, pitch, 100000.f);

            f32 curSampleFrac = s->currentSampleFrac;
            f32 vol[2] = {s->volume[0], s->volume[1]};

            for(s32 written = 0; written < samplesToWrite;){ // This for is only useful if the sound loops.
                f32 srcSamplesLeft = (f32)(srcNumSamples - curSample) - s->currentSampleFrac;
                s32 maxSamplesToWrite = 1 + (s32)Floor(srcSamplesLeft/pitch); // (The 1 is the firstsample at the curSample position)
                s32 soundSamplesToWrite = MinS32(samplesToWrite - written, maxSamplesToWrite);
                s32 soundSamplesToWriteWithoutExtra = ClampS32(samplesToWriteWithoutExtra - written,
                                                               0, maxSamplesToWrite);

                f32 offsetLimit = (soundSamplesToWrite)*pitch; // (The last src sample will be at offsetLimit - pitch).
                f32 d = 1.f/(f32)(soundSamplesToWrite);
                for(s32 i = 0; i < soundSamplesToWrite; i++){
                    f32 t = i*d;
                    f32 offset = offsetLimit*t;
                    f32 offsetFrac = offset - (f32)(s32)offset;
                    s32 carry = (s32)(curSampleFrac + offsetFrac);
                    f32 sampleFrac = curSampleFrac + (offsetFrac - (f32)carry);

                    s16 *sample = srcScan + (srcNumChannels)*((s32)offset + carry);
                    s16 *nextSample = sample + srcNumChannels;

                    *sumScan++ += Lerp((f32)*sample, (f32)*nextSample, sampleFrac)*vol[0];
                    *sumScan++ += Lerp((f32)*(sample + srcIsStereo), (f32)*(nextSample + srcIsStereo), sampleFrac)*vol[1];
                }

                written += soundSamplesToWrite;

                // Advance non-extra
                s32 carry = (s32)(s->currentSampleFrac + Frac(soundSamplesToWriteWithoutExtra*pitch));
                s->currentSample += soundSamplesToWriteWithoutExtra + carry;
                s->currentSampleFrac = Frac(s->currentSampleFrac + Frac(soundSamplesToWriteWithoutExtra*pitch));

                // Advance extra
                carry = (s32)(curSampleFrac + Frac(offsetLimit));
                srcScan += (s32)offsetLimit + carry;
                curSample += (s32)offsetLimit + carry;
                curSampleFrac = Frac(curSampleFrac + Frac(offsetLimit));

                if (s->loop){
                    if (curSample >= srcNumSamples){
                        curSample %= srcNumSamples;
                        srcScan = loadedSound->mem + curSample;
                        if (s->currentSample >= srcNumSamples)
                            s->currentSample %= srcNumSamples;
                    }
                }else{
                    if (s->currentSample >= srcNumSamples){
                        goto LABEL_FinishSound;
                    }
                    break;
                }
            }
        }else{
// Modulated pitch (constant & modulated volume) (loop & no loop)
            // @TODO Optimize this maybe
            
            s16 *lastSample = loadedSound->mem + (loadedSound->numSamples - 1)*srcNumChannels;
            s16 *nextSample = loadedSound->mem + ((curSample + 1) % srcNumSamples)*srcNumChannels;

            f32 curSampleFrac = s->currentSampleFrac;
            f32 vol[2] = {s->volume[0], s->volume[1]}; // extra samples' volume
            f32 dVolume[2] = {s->dVolume[0], s->dVolume[1]};
            f32 volumeTarget[2] = {s->volumeTarget[0], s->volumeTarget[1]};
            f32 pitch = s->pitch;  // extra samples' pitch
            f32 pitchTarget = s->pitchTarget;
            f32 dPitch = s->dPitch;

            // Non-extra samples
            s32 written = 0;
            for(; written < samplesToWriteWithoutExtra; written++){
                s16 *srcNext = srcScan + srcNumChannels;
                if (srcScan >= lastSample){
                    if (!s->loop) goto LABEL_FinishSound;

                    if (srcScan == lastSample){
                        srcNext = loadedSound->mem;
                    }else{ // Wrap
                        s32 pos = (s32)(((umm)srcScan - (umm)loadedSound->mem)/(sizeof(s16)*srcNumChannels));
                        srcScan = loadedSound->mem + (pos % srcNumSamples)*srcNumChannels;
                        srcNext = srcScan + srcNumChannels;
                    }
                }

                *sumScan++ += Lerp((f32)*srcScan, (f32)*srcNext, curSampleFrac)*vol[0]; // L
                *sumScan++ += Lerp((f32)*(srcScan + srcIsStereo), (f32)*(srcNext + srcIsStereo), curSampleFrac)*vol[1]; // R

                curSampleFrac += pitch;
                s32 carry = (s32)curSampleFrac;
                curSampleFrac -= (f32)carry;
                srcScan += carry*srcNumChannels;
                
                vol[0] = MoveValueTo(vol[0], volumeTarget[0], dVolume[0]);
                vol[1] = MoveValueTo(vol[1], volumeTarget[1], dVolume[1]);
                pitch = MoveValueTo(pitch, pitchTarget, dPitch);
            }

            s->currentSample = (s32)(((umm)srcScan - (umm)loadedSound->mem)/(sizeof(s16)*srcNumChannels));
            s->currentSampleFrac = curSampleFrac;
            s->volume[0] = vol[0];
            s->volume[1] = vol[1];
            s->pitch = pitch;
                
            // Extra samples
            for(; written < samplesToWrite; written++){
                s16 *srcNext = srcScan + srcNumChannels;
                if (srcScan >= lastSample){
                    if (!s->loop) goto LABEL_FinishSound;

                    if (srcScan == lastSample){
                        srcNext = loadedSound->mem;
                    }else{ // Wrap
                        s32 pos = (s32)(((umm)srcScan - (umm)loadedSound->mem)/(sizeof(s16)*srcNumChannels));
                        srcScan = loadedSound->mem + (pos % srcNumSamples)*srcNumChannels;
                        srcNext = srcScan + srcNumChannels;
                    }
                }

                *sumScan++ += Lerp((f32)*srcScan, (f32)*srcNext, curSampleFrac)*vol[0]; // L
                *sumScan++ += Lerp((f32)*(srcScan + srcIsStereo), (f32)*(srcNext + srcIsStereo), curSampleFrac)*vol[1]; // R

                curSampleFrac += pitch;
                s32 carry = (s32)curSampleFrac;
                curSampleFrac -= (f32)carry;
                srcScan += carry*srcNumChannels;
                
                vol[0] = MoveValueTo(vol[0], volumeTarget[0], dVolume[0]);
                vol[1] = MoveValueTo(vol[1], volumeTarget[1], dVolume[1]);
                pitch = MoveValueTo(pitch, pitchTarget, dPitch);
            }
        }
        

        if (s->finishIfVolumeGoesTo0 &&
            s->volume[0] == 0 && s->volume[1] == 0 &&
            s->volumeTarget[0] <= 0 && s->volumeTarget[1] <= 0)
        {
            goto LABEL_FinishSound;
        }

        prevPtr = &s->next;
        s       = s->next;
        continue;

    LABEL_FinishSound:
        // Remove from Playing Sounds list.
        *prevPtr = s->next;
        // Insert in Timeout Sounds list.
        ZeroStruct(s);
        s->timeoutTimer = PLAYING_SOUND_DEFAULT_TIMEOUT_TIME_STEPS;
        s->next = state->firstTimeoutSound;
        state->firstTimeoutSound = s;

        s = *prevPtr;
        continue;
    }

    
    // Write sum to output buffer
    f32 masterGainSpeed = (1.f/41100.f) / .1f; // Used to change the volume softly.

    memset((void *)outBuffer->buffer, 0, outBuffer->bufferSize);
    s16 *bufScan = outBuffer->buffer;
    f32 *sumScan = sumBuffer;
    f32 prevSample = *sumScan;
    f32 gain = state->masterGain;
    for(int i = 0; i < maxSamplesToWrite; i++){
        f32 newSample = *sumScan;

        (*bufScan++) = (s16)Clamp((*sumScan++)*gain, -32768.0f, 32767.0f);
        (*bufScan++) = (s16)Clamp((*sumScan++)*gain, -32768.0f, 32767.0f);
    
        gain = MoveValueTo(gain, state->masterGainTarget, masterGainSpeed);
        if (i < maxSamplesToWriteWithoutExtra){
            state->masterGain = gain;
        }

        prevSample = newSample;
    }
     
    
    local_persist f32 lastSample =      *(sumBuffer + (maxSamplesToWriteWithoutExtra*2));
    f32 newLastSample =                 *(sumBuffer + (maxSamplesToWriteWithoutExtra*2));
    local_persist f32 lastSampleExtra = *(sumBuffer + (maxSamplesToWrite*2));
    f32 newLastSampleExtra =            *(sumBuffer + (maxSamplesToWrite*2));
    f32 firstSample = *sumBuffer;
    lastSample = newLastSample;

    Assert(maxSamplesToWrite < 65536);
    outBuffer->samplesWritten = (u16)maxSamplesToWrite;
}
