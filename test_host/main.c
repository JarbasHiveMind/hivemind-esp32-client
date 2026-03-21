/**
 * @file main.c
 * @brief Unity test runner for host-based crypto and binary protocol tests.
 *
 * TEST_CASE macros expand to test_<FILE_ID>_<LINE> functions via unity_config.h.
 * Each test file has a unique TEST_FILE_ID set via CMake COMPILE_DEFINITIONS.
 */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* test_binary.c — TEST_FILE_ID=bin */
void test_bin_9(void);   /* encode and decode BUS message */
void test_bin_34(void);  /* encode and decode BINARY message */
void test_bin_55(void);  /* msg_type_str returns correct strings */
void test_bin_63(void);  /* msg_type_parse roundtrip */
void test_bin_78(void);  /* encode with empty metadata */
void test_bin_97(void);  /* encode and decode large 4KB payload */
/* cross-platform interop tests */
void test_bin_120(void); /* known vector decode */
void test_bin_142(void); /* roundtrip BUS */
void test_bin_165(void); /* roundtrip PROPAGATE */
void test_bin_185(void); /* roundtrip ESCALATE */
void test_bin_205(void); /* roundtrip PING */
void test_bin_224(void); /* roundtrip BINARY RAW_AUDIO */
void test_bin_245(void); /* versioned encode-decode */
void test_bin_266(void); /* large 4KB payload interop */
void test_bin_290(void); /* bin_type UNDEFINED */
void test_bin_305(void); /* bin_type NUMPY_IMAGE */
void test_bin_320(void); /* bin_type FILE */
void test_bin_335(void); /* bin_type STT_TRANSCRIBE */
void test_bin_350(void); /* bin_type STT_HANDLE */
void test_bin_365(void); /* bin_type TTS_AUDIO */
void test_bin_380(void); /* metadata preservation */

/* test_protocol.c — TEST_FILE_ID=proto */
void test_proto_14(void);   /* init sets CONNECTING state */
void test_proto_22(void);   /* init stores password and site_id */
void test_proto_31(void);   /* init generates 36-char session_id */
void test_proto_44(void);   /* init sets preferred cipher and encoding */
void test_proto_58(void);   /* build envelope contains msg_type and payload */
void test_proto_88(void);   /* build envelope with null payload */
void test_proto_103(void);  /* build envelope buffer overflow */
void test_proto_119(void);  /* hello transitions to HELLO_RECEIVED */
void test_proto_135(void);  /* shake request sends envelope reply */
void test_proto_160(void);  /* shake response reaches READY */
void test_proto_193(void);  /* invalid JSON returns error */
void test_proto_204(void);  /* wrong message type in CONNECTING */
void test_proto_220(void);  /* encrypt not-ready returns error */
void test_proto_232(void);  /* decrypt not-ready returns error */
void test_proto_244(void);  /* encrypt-decrypt roundtrip */

/* test_vad_simple.c — TEST_FILE_ID=vad */
void test_vad_12(void);   /* vad detects silence in zero buffer */
void test_vad_19(void);   /* vad detects speech in loud signal */
void test_vad_29(void);   /* vad detects silence in low noise */
void test_vad_39(void);   /* vad handles empty buffer */
void test_vad_45(void);   /* vad detects speech at threshold boundary */
void test_vad_55(void);   /* vad silence at threshold boundary */
void test_vad_65(void);   /* vad handles single sample speech */
void test_vad_71(void);   /* vad handles single sample silence */

/* test_wav_header.c — TEST_FILE_ID=wav */
void test_wav_53(void);   /* wav header has correct RIFF marker */
void test_wav_61(void);   /* wav header has correct file size */
void test_wav_71(void);   /* wav header has correct sample rate */
void test_wav_80(void);   /* wav header has correct format */
void test_wav_93(void);   /* wav find_data locates data chunk */
void test_wav_106(void);  /* wav find_data rejects non-WAV */
void test_wav_116(void);  /* wav find_data rejects too-short buffer */
void test_wav_124(void);  /* wav roundtrip build then parse */

/* test_audio_util.c — TEST_FILE_ID=autil */
void test_autil_31(void);   /* audio_is_wav detects RIFF header */
void test_autil_38(void);   /* audio_is_wav rejects non-WAV */
void test_autil_45(void);   /* audio_is_wav rejects short buffer */
void test_autil_51(void);   /* audio_wav_extract_pcm finds data */
void test_autil_72(void);   /* audio_wav_extract_pcm rejects non-WAV */
void test_autil_81(void);   /* audio_apply_volume full volume is identity */
void test_autil_90(void);   /* audio_apply_volume zero volume is silence */
void test_autil_99(void);   /* audio_apply_volume 50 percent halves amplitude */
void test_autil_108(void);  /* audio_apply_volume small volume */

/* test_crypto.c — TEST_FILE_ID=crypto */
void test_crypto_18(void);   /* hsub generation */
void test_crypto_28(void);   /* hsub IV extraction */
void test_crypto_39(void);   /* key derivation */
void test_crypto_52(void);   /* AES-GCM roundtrip */
void test_crypto_78(void);   /* ChaCha20-Poly1305 roundtrip */
void test_crypto_104(void);  /* JSON-HEX roundtrip */
void test_crypto_129(void);  /* binary encrypt roundtrip */
void test_crypto_155(void);  /* cipher name parse */

int main(void)
{
    UNITY_BEGIN();

    /* Binary tests */
    RUN_TEST(test_bin_9);
    RUN_TEST(test_bin_34);
    RUN_TEST(test_bin_55);
    RUN_TEST(test_bin_63);
    RUN_TEST(test_bin_78);
    RUN_TEST(test_bin_97);

    /* Binary interop tests */
    RUN_TEST(test_bin_120);
    RUN_TEST(test_bin_142);
    RUN_TEST(test_bin_165);
    RUN_TEST(test_bin_185);
    RUN_TEST(test_bin_205);
    RUN_TEST(test_bin_224);
    RUN_TEST(test_bin_245);
    RUN_TEST(test_bin_266);
    RUN_TEST(test_bin_290);
    RUN_TEST(test_bin_305);
    RUN_TEST(test_bin_320);
    RUN_TEST(test_bin_335);
    RUN_TEST(test_bin_350);
    RUN_TEST(test_bin_365);
    RUN_TEST(test_bin_380);

    /* Crypto tests */
    RUN_TEST(test_crypto_18);
    RUN_TEST(test_crypto_28);
    RUN_TEST(test_crypto_39);
    RUN_TEST(test_crypto_52);
    RUN_TEST(test_crypto_78);
    RUN_TEST(test_crypto_104);
    RUN_TEST(test_crypto_129);
    RUN_TEST(test_crypto_155);

    /* VAD tests */
    RUN_TEST(test_vad_12);
    RUN_TEST(test_vad_19);
    RUN_TEST(test_vad_29);
    RUN_TEST(test_vad_39);
    RUN_TEST(test_vad_45);
    RUN_TEST(test_vad_55);
    RUN_TEST(test_vad_65);
    RUN_TEST(test_vad_71);

    /* WAV header tests */
    RUN_TEST(test_wav_53);
    RUN_TEST(test_wav_61);
    RUN_TEST(test_wav_71);
    RUN_TEST(test_wav_80);
    RUN_TEST(test_wav_93);
    RUN_TEST(test_wav_106);
    RUN_TEST(test_wav_116);
    RUN_TEST(test_wav_124);

    /* Audio util tests */
    RUN_TEST(test_autil_31);
    RUN_TEST(test_autil_38);
    RUN_TEST(test_autil_45);
    RUN_TEST(test_autil_51);
    RUN_TEST(test_autil_72);
    RUN_TEST(test_autil_81);
    RUN_TEST(test_autil_90);
    RUN_TEST(test_autil_99);
    RUN_TEST(test_autil_108);

    /* Protocol tests */
    RUN_TEST(test_proto_14);
    RUN_TEST(test_proto_22);
    RUN_TEST(test_proto_31);
    RUN_TEST(test_proto_44);
    RUN_TEST(test_proto_58);
    RUN_TEST(test_proto_88);
    RUN_TEST(test_proto_103);
    RUN_TEST(test_proto_119);
    RUN_TEST(test_proto_135);
    RUN_TEST(test_proto_160);
    RUN_TEST(test_proto_193);
    RUN_TEST(test_proto_204);
    RUN_TEST(test_proto_220);
    RUN_TEST(test_proto_232);
    RUN_TEST(test_proto_244);

    return UNITY_END();
}
