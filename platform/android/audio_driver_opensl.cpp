/**************************************************************************/
/*  audio_driver_opensl.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_driver_opensl.h"

#include <string.h>

#include <jni.h>
#include "core/os/thread.h"
#include "thread_jandroid.h"

#define MAX_NUMBER_INTERFACES 3
#define MAX_NUMBER_OUTPUT_DEVICES 6

// --- Captura por JNI AudioRecord -------------------------------------------
// OpenSL entrega silencio en algunas ROMs (el callback corre pero el buffer es
// cero). Se captura con android.media.AudioRecord vía JNI y se escribe en el
// input buffer del AudioServer, igual que hacía el callback de OpenSL.
static jobject s_ar = nullptr;
static jclass s_ar_class = nullptr;
static jmethodID s_ar_read = nullptr;
static jmethodID s_ar_stop = nullptr;
static jmethodID s_ar_release = nullptr;
static Thread *s_ar_thread = nullptr;
static volatile bool s_ar_run = false;
static AudioDriverOpenSL *s_ar_driver = nullptr;
static const int AR_RATE = 44100;
static const int AR_FRAMES = 1024;

static void _ar_capture_trampoline(void *p_ud) {
	((AudioDriverOpenSL *)p_ud)->_ar_capture_loop();
}

/* Structure for passing information to callback function */

void AudioDriverOpenSL::_buffer_callback(
		SLAndroidSimpleBufferQueueItf queueItf) {
	bool mix = true;

	if (pause) {
		mix = false;
	} else {
		mix = mutex.try_lock() == OK;
	}

	if (mix) {
		audio_server_process(buffer_size, mixdown_buffer);
	} else {
		int32_t *src_buff = mixdown_buffer;
		for (unsigned int i = 0; i < buffer_size * 2; i++) {
			src_buff[i] = 0;
		}
	}

	if (mix) {
		mutex.unlock();
	}

	const int32_t *src_buff = mixdown_buffer;

	int16_t *ptr = (int16_t *)buffers[last_free];
	last_free = (last_free + 1) % BUFFER_COUNT;

	for (unsigned int i = 0; i < buffer_size * 2; i++) {
		ptr[i] = src_buff[i] >> 16;
	}

	(*queueItf)->Enqueue(queueItf, ptr, 4 * buffer_size);
}

void AudioDriverOpenSL::_buffer_callbacks(
		SLAndroidSimpleBufferQueueItf queueItf,
		void *pContext) {
	AudioDriverOpenSL *ad = (AudioDriverOpenSL *)pContext;

	ad->_buffer_callback(queueItf);
}

const char *AudioDriverOpenSL::get_name() const {
	return "Android";
}

Error AudioDriverOpenSL::init() {
	SLresult res;
	SLEngineOption EngineOption[] = {
		{ (SLuint32)SL_ENGINEOPTION_THREADSAFE, (SLuint32)SL_BOOLEAN_TRUE }
	};
	res = slCreateEngine(&sl, 1, EngineOption, 0, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(res != SL_RESULT_SUCCESS, ERR_INVALID_PARAMETER, "Could not initialize OpenSL.");

	res = (*sl)->Realize(sl, SL_BOOLEAN_FALSE);
	ERR_FAIL_COND_V_MSG(res != SL_RESULT_SUCCESS, ERR_INVALID_PARAMETER, "Could not realize OpenSL.");

	return OK;
}

void AudioDriverOpenSL::start() {
	active = false;

	SLresult res;

	buffer_size = 1024;

	for (int i = 0; i < BUFFER_COUNT; i++) {
		buffers[i] = memnew_arr(int16_t, buffer_size * 2);
		memset(buffers[i], 0, buffer_size * 4);
	}

	mixdown_buffer = memnew_arr(int32_t, buffer_size * 2);

	/* Callback context for the buffer queue callback function */

	/* Get the SL Engine Interface which is implicit */
	res = (*sl)->GetInterface(sl, SL_IID_ENGINE, (void *)&EngineItf);

	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);

	{
		const SLInterfaceID ids[1] = { SL_IID_ENVIRONMENTALREVERB };
		const SLboolean req[1] = { SL_BOOLEAN_FALSE };
		res = (*EngineItf)->CreateOutputMix(EngineItf, &OutputMix, 0, ids, req);
	}

	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);
	// Realizing the Output Mix object in synchronous mode.
	res = (*OutputMix)->Realize(OutputMix, SL_BOOLEAN_FALSE);
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);

	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, BUFFER_COUNT };
	/* Setup the format of the content in the buffer queue */
	pcm.formatType = SL_DATAFORMAT_PCM;
	pcm.numChannels = 2;
	pcm.samplesPerSec = SL_SAMPLINGRATE_44_1;
	pcm.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
	pcm.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
	pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
#ifdef BIG_ENDIAN_ENABLED
	pcm.endianness = SL_BYTEORDER_BIGENDIAN;
#else
	pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;
#endif
	audioSource.pFormat = (void *)&pcm;
	audioSource.pLocator = (void *)&loc_bufq;

	/* Setup the data sink structure */
	locator_outputmix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
	locator_outputmix.outputMix = OutputMix;
	audioSink.pLocator = (void *)&locator_outputmix;
	audioSink.pFormat = nullptr;

	/* Create the music player */
	{
		const SLInterfaceID ids[2] = { SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND };
		const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };

		res = (*EngineItf)->CreateAudioPlayer(EngineItf, &player, &audioSource, &audioSink, 1, ids, req);
		ERR_FAIL_COND(res != SL_RESULT_SUCCESS);
	}
	/* Realizing the player in synchronous mode. */
	res = (*player)->Realize(player, SL_BOOLEAN_FALSE);
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);
	/* Get seek and play interfaces */
	res = (*player)->GetInterface(player, SL_IID_PLAY, (void *)&playItf);
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);
	res = (*player)->GetInterface(player, SL_IID_BUFFERQUEUE,
			(void *)&bufferQueueItf);
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);
	/* Setup to receive buffer queue event callbacks */
	res = (*bufferQueueItf)->RegisterCallback(bufferQueueItf, _buffer_callbacks, this);
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);

	last_free = 0;

	//fill up buffers
	for (int i = 0; i < BUFFER_COUNT; i++) {
		/* Enqueue a few buffers to get the ball rolling */
		res = (*bufferQueueItf)->Enqueue(bufferQueueItf, buffers[i], 4 * buffer_size); /* Size given in */
	}

	res = (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);

	active = true;
}

void AudioDriverOpenSL::_record_buffer_callback(SLAndroidSimpleBufferQueueItf queueItf) {
	static bool logged = false;
	if (!logged) {
		logged = true;
		int peak = 0;
		for (int i = 0; i < rec_buffer.size(); i++) {
			int v = rec_buffer[i];
			if (v < 0) {
				v = -v;
			}
			if (v > peak) {
				peak = v;
			}
		}
		print_line("XATDBG opensl rec first peak=" + String::num_int64(peak));
	}
	for (int i = 0; i < rec_buffer.size(); i++) {
		int32_t sample = rec_buffer[i] << 16;
		input_buffer_write(sample); // el buffer de entrada ya es estéreo intercalado
	}

	SLresult res = (*queueItf)->Enqueue(queueItf, rec_buffer.ptrw(), rec_buffer.size() * sizeof(int16_t));
	ERR_FAIL_COND(res != SL_RESULT_SUCCESS);
}

void AudioDriverOpenSL::_record_buffer_callbacks(SLAndroidSimpleBufferQueueItf queueItf, void *pContext) {
	AudioDriverOpenSL *ad = (AudioDriverOpenSL *)pContext;

	ad->_record_buffer_callback(queueItf);
}

Error AudioDriverOpenSL::capture_init_device() {
	SLDataLocator_IODevice loc_dev = {
		SL_DATALOCATOR_IODEVICE,
		SL_IODEVICE_AUDIOINPUT,
		SL_DEFAULTDEVICEID_AUDIOINPUT,
		nullptr
	};
	SLDataSource recSource = { &loc_dev, nullptr };

	SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
		SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
		2
	};
	SLDataFormat_PCM format_pcm = {
		SL_DATAFORMAT_PCM,
		2,
		SL_SAMPLINGRATE_44_1,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
		SL_BYTEORDER_LITTLEENDIAN
	};
	SLDataSink recSnk = { &loc_bq, &format_pcm };

	const SLInterfaceID ids[2] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION };
	const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };

	SLresult res = (*EngineItf)->CreateAudioRecorder(EngineItf, &recorder, &recSource, &recSnk, 2, ids, req);
	print_line("XATDBG opensl CreateAudioRecorder=" + String::num_int64((int64_t)res));
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	// Preset de grabación: en algunas ROMs el preset por defecto captura
	// silencio (micrófono devuelve ceros). VOICE_RECOGNITION es lo más
	// compatible para notas de voz y debe setearse antes de Realize.
	SLAndroidConfigurationItf configItf;
	if ((*recorder)->GetInterface(recorder, SL_IID_ANDROIDCONFIGURATION, (void *)&configItf) == SL_RESULT_SUCCESS) {
		SLint32 preset = SL_ANDROID_RECORDING_PRESET_VOICE_RECOGNITION;
		(*configItf)->SetConfiguration(configItf, SL_ANDROID_KEY_RECORDING_PRESET, &preset, sizeof(preset));
	}

	res = (*recorder)->Realize(recorder, SL_BOOLEAN_FALSE);
	print_line("XATDBG opensl Realize=" + String::num_int64((int64_t)res));
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	res = (*recorder)->GetInterface(recorder, SL_IID_RECORD, (void *)&recordItf);
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	res = (*recorder)->GetInterface(recorder, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, (void *)&recordBufferQueueItf);
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	res = (*recordBufferQueueItf)->RegisterCallback(recordBufferQueueItf, _record_buffer_callbacks, this);
	print_line("XATDBG opensl RegisterCallback=" + String::num_int64((int64_t)res));
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	SLuint32 state;
	res = (*recordItf)->GetRecordState(recordItf, &state);
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	if (state != SL_RECORDSTATE_STOPPED) {
		res = (*recordItf)->SetRecordState(recordItf, SL_RECORDSTATE_STOPPED);
		ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

		res = (*recordBufferQueueItf)->Clear(recordBufferQueueItf);
		ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);
	}

	const int rec_buffer_frames = 2048;
	rec_buffer.resize(rec_buffer_frames);
	input_buffer_init(rec_buffer_frames);

	res = (*recordBufferQueueItf)->Enqueue(recordBufferQueueItf, rec_buffer.ptrw(), rec_buffer.size() * sizeof(int16_t));
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	res = (*recordItf)->SetRecordState(recordItf, SL_RECORDSTATE_RECORDING);
	print_line("XATDBG opensl SetRecordState=" + String::num_int64((int64_t)res));
	ERR_FAIL_COND_V(res != SL_RESULT_SUCCESS, ERR_CANT_OPEN);

	return OK;
}

void AudioDriverOpenSL::_ar_capture_loop() {
	JNIEnv *env = get_jni_env();
	if (env == nullptr) {
		return;
	}
	jshortArray arr = env->NewShortArray(AR_FRAMES);
	bool logged = false;
	while (s_ar_run) {
		int n = env->CallIntMethod(s_ar, s_ar_read, arr, 0, AR_FRAMES);
		if (!logged) {
			logged = true;
			print_line("XATDBG ar read n=" + String::num_int64(n));
		}
		if (env->ExceptionCheck()) {
			env->ExceptionDescribe();
			env->ExceptionClear();
			break;
		}
		if (n > 0) {
			jshort *buf = env->GetShortArrayElements(arr, nullptr);
			if (buf != nullptr) {
				for (int i = 0; i < n; i++) {
					int32_t smp = ((int32_t)buf[i]) << 16;
					input_buffer_write(smp);
					input_buffer_write(smp); // mono -> estéreo (L=R)
				}
				env->ReleaseShortArrayElements(arr, buf, JNI_ABORT);
			}
		}
	}
	env->DeleteLocalRef(arr);
}

Error AudioDriverOpenSL::capture_start() {
	print_line("XATDBG ar capture_start ENTRY");
	if (!(bool)OS::get_singleton()->request_permission("RECORD_AUDIO")) {
		WARN_PRINT("Unable to start audio capture - No RECORD_AUDIO permission");
		return ERR_UNAUTHORIZED;
	}
	print_line("XATDBG ar perm ok");
	if (s_ar != nullptr) {
		print_line("XATDBG ar already capturing");
		return OK; // ya capturando
	}
	JNIEnv *env = get_jni_env();
	print_line("XATDBG ar env=" + String::num_int64((int64_t)(intptr_t)env));
	if (env == nullptr) {
		return ERR_CANT_OPEN;
	}
	if (s_ar_class == nullptr) {
		jclass local = env->FindClass("android/media/AudioRecord");
		print_line("XATDBG ar FindClass=" + String::num_int64((int64_t)(intptr_t)local));
		if (local == nullptr) {
			env->ExceptionDescribe();
			env->ExceptionClear();
			return ERR_CANT_OPEN;
		}
		s_ar_class = (jclass)env->NewGlobalRef(local);
		env->DeleteLocalRef(local);
	}
	if (s_ar_class == nullptr) {
		return ERR_CANT_OPEN;
	}
	jmethodID min = env->GetStaticMethodID(s_ar_class, "getMinBufferSize", "(III)I");
	jmethodID ctor = env->GetMethodID(s_ar_class, "<init>", "(IIIII)V");
	jmethodID startm = env->GetMethodID(s_ar_class, "startRecording", "()V");
	s_ar_read = env->GetMethodID(s_ar_class, "read", "([SII)I");
	s_ar_stop = env->GetMethodID(s_ar_class, "stop", "()V");
	s_ar_release = env->GetMethodID(s_ar_class, "release", "()V");
	if (min == nullptr || ctor == nullptr || startm == nullptr || s_ar_read == nullptr) {
		env->ExceptionClear();
		return ERR_CANT_OPEN;
	}
	const int CHANNEL_IN_MONO = 16; // AudioFormat.CHANNEL_IN_MONO
	const int ENCODING_PCM_16BIT = 2; // AudioFormat.ENCODING_PCM_16BIT
	const int MIC = 1; // MediaRecorder.AudioSource.MIC
	int minbuf = env->CallStaticIntMethod(s_ar_class, min, AR_RATE, CHANNEL_IN_MONO, ENCODING_PCM_16BIT);
	print_line("XATDBG ar minbuf=" + String::num_int64(minbuf));
	if (minbuf <= 0) {
		minbuf = AR_FRAMES * 4;
	}
	jobject ar = env->NewObject(s_ar_class, ctor, MIC, AR_RATE, CHANNEL_IN_MONO, ENCODING_PCM_16BIT, minbuf);
	if (ar == nullptr) {
		print_line("XATDBG ar NewObject null");
		env->ExceptionDescribe();
		env->ExceptionClear();
		return ERR_CANT_OPEN;
	}
	env->CallVoidMethod(ar, startm);
	if (env->ExceptionCheck()) {
		print_line("XATDBG ar startRecording exception");
		env->ExceptionDescribe();
		env->ExceptionClear();
		env->DeleteLocalRef(ar);
		return ERR_CANT_OPEN;
	}
	print_line("XATDBG ar startRecording ok");
	s_ar = env->NewGlobalRef(ar);
	env->DeleteLocalRef(ar);
	if (s_ar == nullptr) {
		return ERR_CANT_OPEN;
	}
	input_buffer_init(AR_RATE / 10);
	s_ar_driver = this;
	s_ar_run = true;
	s_ar_thread = memnew(Thread);
	s_ar_thread->start(_ar_capture_trampoline, this);
	return OK;
}

Error AudioDriverOpenSL::capture_stop() {
	if (s_ar_thread != nullptr) {
		s_ar_run = false;
		s_ar_thread->wait_to_finish();
		memdelete(s_ar_thread);
		s_ar_thread = nullptr;
	}
	JNIEnv *env = get_jni_env();
	if (s_ar != nullptr && env != nullptr) {
		if (s_ar_stop != nullptr) {
			env->CallVoidMethod(s_ar, s_ar_stop);
			if (env->ExceptionCheck()) {
				env->ExceptionClear();
			}
		}
		if (s_ar_release != nullptr) {
			env->CallVoidMethod(s_ar, s_ar_release);
			if (env->ExceptionCheck()) {
				env->ExceptionClear();
			}
		}
		env->DeleteGlobalRef(s_ar);
		s_ar = nullptr;
	}
	return OK;
}

int AudioDriverOpenSL::get_mix_rate() const {
	return 44100; // hardcoded for Android, as selected by SL_SAMPLINGRATE_44_1
}

AudioDriver::SpeakerMode AudioDriverOpenSL::get_speaker_mode() const {
	return SPEAKER_MODE_STEREO;
}

void AudioDriverOpenSL::lock() {
	if (active) {
		mutex.lock();
	}
}

void AudioDriverOpenSL::unlock() {
	if (active) {
		mutex.unlock();
	}
}

void AudioDriverOpenSL::finish() {
	(*sl)->Destroy(sl);
}

void AudioDriverOpenSL::set_pause(bool p_pause) {
	pause = p_pause;

	if (active) {
		if (pause) {
			(*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PAUSED);
		} else {
			(*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
		}
	}
}

AudioDriverOpenSL::AudioDriverOpenSL() {
}
