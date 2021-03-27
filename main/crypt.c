/*
Copyright (c) 2008-2012
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "mbedtls/blowfish.h"
#include "crypt.h"

// Remove
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// Decrypt a hex-encoded, Blowfish ECB-encrypted string
char *BlowfishDecryptString (
	const char * const input,
	size_t * const retSize) 
{
	size_t inputLen = strlen (input);
	unsigned char *output;
	unsigned char *decrypted = NULL;
	size_t outputLen = inputLen/2;
	const char *decrypt_key = "R=U!LH$O2B#";
	mbedtls_blowfish_context ctx;
    char hex[3];
    hex[2] = '\0';

	assert(inputLen % 2 == 0);

	printf("input len = %d   output len = %d\n", inputLen, outputLen);

	output = calloc(outputLen+1, sizeof (*output));
	/* hex decode */
	for (size_t i = 0; i < outputLen; i++) {
		memcpy (hex, &input[i*2], 2);	
		output[i] = strtol (hex, NULL, 16);
		printf ("size=%d i=%d Convert %s to %d \n", sizeof(size_t), i, hex, (int)output[i]);
		vTaskDelay(250 / portTICK_PERIOD_MS);
	}

	decrypted = calloc(outputLen + 1, sizeof(*decrypted));

	mbedtls_blowfish_init(&ctx);
	if (mbedtls_blowfish_setkey(&ctx, (const unsigned char*)decrypt_key, strlen(decrypt_key) * 8))
		printf("setkey failed!\n");
	vTaskDelay(250 / portTICK_PERIOD_MS);

	for (int i = 0; i < outputLen; i += MBEDTLS_BLOWFISH_BLOCKSIZE) {
		if (mbedtls_blowfish_crypt_ecb(&ctx, MBEDTLS_BLOWFISH_DECRYPT, output + i, decrypted + i))
			printf ("mbedtls_blowfish_crypt_ecb failed! i=%d", i);
		vTaskDelay(250 / portTICK_PERIOD_MS);

	}

	free(output);
	mbedtls_blowfish_free(&ctx);

	*retSize = outputLen;

	return (char *) decrypted;
}


// Encrypt "plain" using Blowfish ECB, then hex-encode it
char *BlowfishEncryptString (
	const char *plain) 
{
	unsigned char *in, *out, *hex_out;
	mbedtls_blowfish_context ctx;
	const char *encrypt_key = "6#26FRL$ZWD";

	size_t plain_len = strlen(plain);
	/* blowfish expects two 32 bit blocks */
	size_t in_len = (plain_len % 8 == 0) ? plain_len : plain_len + (8-plain_len%8);

	in = calloc (in_len+1, sizeof(*in));
	memcpy (in, plain, plain_len);
	out = calloc (in_len, sizeof(*out));

	mbedtls_blowfish_init(&ctx);
	mbedtls_blowfish_setkey(&ctx, (const unsigned char*)encrypt_key, strlen(encrypt_key) * 8);

	for (int i = 0; i < in_len; i += MBEDTLS_BLOWFISH_BLOCKSIZE) {
		mbedtls_blowfish_crypt_ecb(&ctx, MBEDTLS_BLOWFISH_ENCRYPT, in + i, out + i);
	}

	// Convert to hex
	hex_out = calloc (in_len*2+1, sizeof (*hex_out));
	for (size_t i = 0; i < in_len; i++) {
		snprintf ((char * restrict) &hex_out[i*2], 3, "%02x", in[i]);
	}

	// Cleanup
	free(in);
	free(out);
	mbedtls_blowfish_free(&ctx);

	return (char *) hex_out;
}

