/* Regression tests for Chafa's streaming Base64 encoder.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "config.h"
#include <string.h>
#include <glib.h>
#include "chafa.h"
#include "internal/chafa-base64.h"

static const guint8 prefix [] = { 'p', 0, 'r', 255 };
static guint64 completed;

static void
assert_output (const GString *actual, const GString *expected)
{
    g_assert_cmpuint (actual->len, ==, expected->len);
    g_assert (memcmp (actual->str, expected->str, actual->len) == 0);
    g_assert_cmpint (actual->str [actual->len], ==, 0);
}

static void
feed (ChafaBase64 *enc, GString *out, const guint8 *data,
      gsize n, gsize *fed)
{
    chafa_base64_encode (enc, out, data + *fed, n);
    *fed += n;
    g_assert_cmpint (enc->buf_len, >=, 0);
    g_assert_cmpuint (enc->buf_len, ==, *fed % 3);
    g_assert_cmpuint (out->len, ==, sizeof (prefix) + 4 * (*fed / 3));
    g_assert (memcmp (enc->buf, data + *fed - enc->buf_len, enc->buf_len) == 0);
    g_assert (memcmp (out->str, prefix, sizeof (prefix)) == 0);
    g_assert_cmpint (out->str [out->len], ==, 0);
}

static void
check_case (const guint8 *data, gsize n, gsize split, gsize chunk)
{
    ChafaBase64 enc;
    GString *out = g_string_new_len ((const gchar *) prefix, sizeof (prefix));
    GString *expected = g_string_new_len ((const gchar *) prefix, sizeof (prefix));
    gchar *oracle = g_base64_encode (data, n);
    gsize fed = 0;

    g_assert_cmpuint (split, <=, n);
    chafa_base64_init (&enc);
    feed (&enc, out, data, 0, &fed);
    if (chunk)
    {
        while (fed < n)
        {
            feed (&enc, out, data, MIN (chunk, n - fed), &fed);
            feed (&enc, out, data, 0, &fed);
        }
    }
    else
    {
        feed (&enc, out, data, split, &fed);
        feed (&enc, out, data, 0, &fed);
        feed (&enc, out, data, n - split, &fed);
    }
    chafa_base64_encode_end (&enc, out);
    g_string_append (expected, oracle);
    assert_output (out, expected);
    g_assert_cmpint (enc.buf_len, ==, 0);
    chafa_base64_encode_end (&enc, out);
    assert_output (out, expected);
    chafa_base64_deinit (&enc);
    g_assert_cmpint (enc.buf_len, ==, -1);
    g_assert_cmpuint (enc.buf [0], ==, 0);
    g_assert_cmpuint (enc.buf [1], ==, 0);
    g_string_free (out, TRUE);
    g_string_free (expected, TRUE);
    g_free (oracle);
    completed++;
}

static void
known_vectors_test (void)
{
    static const gchar *inputs [] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
    static const gchar *outputs [] = { "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy" };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (inputs); i++)
    {
        ChafaBase64 enc;
        GString *out = g_string_new (NULL);

        chafa_base64_init (&enc);
        chafa_base64_encode (&enc, out, inputs [i], strlen (inputs [i]));
        chafa_base64_encode_end (&enc, out);
        g_assert_cmpuint (out->len, ==, strlen (outputs [i]));
        g_assert_cmpstr (out->str, ==, outputs [i]);
        chafa_base64_deinit (&enc);
        g_string_free (out, TRUE);
    }
}

static void
exhaustive_short_test (void)
{
    guint8 data [2];
    guint a, b;
    guint64 before = completed;

    for (a = 0; a < 256; a++)
    {
        data [0] = a;
        check_case (data, 1, 0, 0);
        for (b = 0; b < 256; b++)
        {
            data [1] = b;
            check_case (data, 2, 1, 0);
        }
    }
    g_assert_cmpuint (completed - before, ==, 65792);
    g_test_message ("Completed %" G_GUINT64_FORMAT " short-input cases", completed - before);
}

static void
stream_boundaries_test (void)
{
    static const gsize lengths [] = {
        255, 256, 257, 511, 512, 513, 765, 766, 767, 768, 769, 770, 771,
        1023, 1024, 1025, 2047, 2048, 2049, 65535, 65536
    };
    static const gsize chunks [] = { 1, 2, 3, 63, 255, 510, 767, 768, 769, 1024 };
    guint8 *data = g_malloc (65536);
    guint64 before = completed;
    gsize i, j, n, split;

    for (i = 0; i < 65536; i++)
        data [i] = (i * 73) ^ (i >> 3);
    for (n = 0; n <= 128; n++)
        for (split = 0; split <= n; split++)
            check_case (data, n, split, 0);
    for (i = 0; i < G_N_ELEMENTS (lengths); i++)
    {
        gsize splits [6];

        n = lengths [i];
        splits [0] = 0;
        splits [1] = 1;
        splits [2] = 2;
        splits [3] = n / 2;
        splits [4] = n - 1;
        splits [5] = n;
        for (j = 0; j < G_N_ELEMENTS (splits); j++)
            check_case (data, n, splits [j], 0);
        for (j = 0; j < G_N_ELEMENTS (chunks); j++)
            check_case (data, n, 0, chunks [j]);
    }
    g_free (data);
    g_assert_cmpuint (completed - before, ==, 8721);
    g_test_message ("Completed %" G_GUINT64_FORMAT " boundary cases", completed - before);
}

static void
finalize_reuse_test (void)
{
    static const guint8 data [] = { 0, 255, 1, 128, 2, 127 };
    static const gsize lengths [] = { 1, 2, 3, 6 };
    ChafaBase64 enc;
    GString *out = g_string_new_len ((const gchar *) prefix, sizeof (prefix));
    GString *expected = g_string_new_len ((const gchar *) prefix, sizeof (prefix));
    guint i;

    chafa_base64_init (&enc);
    for (i = 0; i < G_N_ELEMENTS (lengths); i++)
    {
        gchar *oracle = g_base64_encode (data, lengths [i]);

        chafa_base64_encode (&enc, out, data, 0);
        chafa_base64_encode (&enc, out, data, lengths [i]);
        g_assert_cmpuint (enc.buf_len, ==, lengths [i] % 3);
        chafa_base64_encode_end (&enc, out);
        g_string_append (expected, oracle);
        assert_output (out, expected);
        g_assert_cmpint (enc.buf_len, ==, 0);
        chafa_base64_encode_end (&enc, out);
        assert_output (out, expected);
        g_free (oracle);
    }
    chafa_base64_deinit (&enc);
    g_assert_cmpint (enc.buf_len, ==, -1);
    g_assert_cmpuint (enc.buf [0], ==, 0);
    g_assert_cmpuint (enc.buf [1], ==, 0);
    g_string_free (out, TRUE);
    g_string_free (expected, TRUE);
}

int
main (int argc, char *argv [])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/base64/known-vectors", known_vectors_test);
    g_test_add_func ("/base64/exhaustive-short", exhaustive_short_test);
    g_test_add_func ("/base64/stream-boundaries", stream_boundaries_test);
    g_test_add_func ("/base64/finalize-reuse", finalize_reuse_test);
    return g_test_run ();
}
