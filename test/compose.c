#include <time.h>

#include "xkbcommon/xkbcommon-compose.h"

#include "test.h"

#define BENCHMARK_ITERATIONS 1000

static void
benchmark(struct xkb_context *ctx)
{
    struct timespec start, stop, elapsed;
    enum xkb_log_level old_level = xkb_context_get_log_level(ctx);
    int old_verb = xkb_context_get_log_verbosity(ctx);
    char *path;
    FILE *file;
    struct xkb_compose_table *table;

    path = test_get_path("compose/en_US.UTF-8/Compose");
    file = fopen(path, "r");

    xkb_context_set_log_level(ctx, XKB_LOG_LEVEL_CRITICAL);
    xkb_context_set_log_verbosity(ctx, 0);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        rewind(file);
        table = xkb_compose_table_new_from_file(ctx, file, "",
                                                XKB_COMPOSE_FORMAT_TEXT_V1,
                                                XKB_COMPOSE_COMPILE_NO_FLAGS);
        assert(table);
        xkb_compose_table_unref(table);
    }
    clock_gettime(CLOCK_MONOTONIC, &stop);

    xkb_context_set_log_level(ctx, old_level);
    xkb_context_set_log_verbosity(ctx, old_verb);

    fclose(file);
    free(path);

    elapsed.tv_sec = stop.tv_sec - start.tv_sec;
    elapsed.tv_nsec = stop.tv_nsec - start.tv_nsec;
    if (elapsed.tv_nsec < 0) {
        elapsed.tv_nsec += 1000000000;
        elapsed.tv_sec--;
    }

    fprintf(stderr, "compiled %d compose tables in %ld.%09lds\n",
            BENCHMARK_ITERATIONS, elapsed.tv_sec, elapsed.tv_nsec);
}

static const char *
compose_status_string(enum xkb_compose_status status)
{
    switch (status) {
    case XKB_COMPOSE_NOTHING:
        return "nothing";
    case XKB_COMPOSE_COMPOSING:
        return "composing";
    case XKB_COMPOSE_COMPOSED:
        return "composed";
    case XKB_COMPOSE_CANCELLED:
        return "cancelled";
    }

    return "<invalid-status>";
}

static const char *
feed_result_string(enum xkb_compose_feed_result result)
{
    switch (result) {
    case XKB_COMPOSE_FEED_IGNORED:
        return "ignored";
    case XKB_COMPOSE_FEED_ACCEPTED:
        return "accepted";
    }

    return "<invalid-result>";
}

/*
 * Feed a sequence of keysyms to a fresh compose state and test the outcome.
 *
 * The varargs consists of lines in the following format:
 *      <input keysym> <expected feed result> <expected status> <expected string> <expected keysym>
 * Terminated by a line consisting only of XKB_KEY_NoSymbol.
 */
static bool
test_compose_seq_va(struct xkb_compose_table *table, va_list ap)
{
    int ret;
    struct xkb_compose_state *state;
    char buffer[64];

    state = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
    assert(state);

    for (int i = 1; ; i++) {
        xkb_keysym_t input_keysym;
        enum xkb_compose_feed_result result, expected_result;
        enum xkb_compose_status status, expected_status;
        const char *expected_string;
        xkb_keysym_t keysym, expected_keysym;

        input_keysym = va_arg(ap, xkb_keysym_t);
        if (input_keysym == XKB_KEY_NoSymbol)
            break;

        expected_result = va_arg(ap, enum xkb_compose_feed_result);
        expected_status = va_arg(ap, enum xkb_compose_status);
        expected_string = va_arg(ap, const char *);
        expected_keysym = va_arg(ap, xkb_keysym_t);

        result = xkb_compose_state_feed(state, input_keysym);

        if (result != expected_result) {
            fprintf(stderr, "after feeding %d keysyms:\n", i);
            fprintf(stderr, "expected feed result: %s\n",
                    feed_result_string(expected_result));
            fprintf(stderr, "got feed result: %s\n",
                    feed_result_string(result));
            goto fail;
        }

        status = xkb_compose_state_get_status(state);
        if (status != expected_status) {
            fprintf(stderr, "after feeding %d keysyms:\n", i);
            fprintf(stderr, "expected status: %s\n",
                    compose_status_string(expected_status));
            fprintf(stderr, "got status: %s\n",
                    compose_status_string(status));
            goto fail;
        }

        ret = xkb_compose_state_get_utf8(state, buffer, sizeof(buffer));
        if (ret < 0 || (size_t) ret >= sizeof(buffer)) {
            fprintf(stderr, "after feeding %d keysyms:\n", i);
            fprintf(stderr, "expected string: %s\n", expected_string);
            fprintf(stderr, "got error: %d\n", ret);
            goto fail;
        }
        if (!streq(buffer, expected_string)) {
            fprintf(stderr, "after feeding %d keysyms:\n", i);
            fprintf(stderr, "expected string: %s\n", strempty(expected_string));
            fprintf(stderr, "got string: %s\n", buffer);
            goto fail;
        }

        keysym = xkb_compose_state_get_one_sym(state);
        if (keysym != expected_keysym) {
            fprintf(stderr, "after feeding %d keysyms:\n", i);
            xkb_keysym_get_name(expected_keysym, buffer, sizeof(buffer));
            fprintf(stderr, "expected keysym: %s\n", buffer);
            xkb_keysym_get_name(keysym, buffer, sizeof(buffer));
            fprintf(stderr, "got keysym (%#x): %s\n", keysym, buffer);
            goto fail;
        }
    }

    xkb_compose_state_unref(state);
    return true;

fail:
    xkb_compose_state_unref(state);
    return false;
}

static bool
test_compose_seq(struct xkb_compose_table *table, ...)
{
    va_list ap;
    bool ok;
    va_start(ap, table);
    ok = test_compose_seq_va(table, ap);
    va_end(ap);
    return ok;
}

static bool
test_compose_seq_buffer(struct xkb_context *ctx, const char *buffer, ...)
{
    va_list ap;
    bool ok;
    struct xkb_compose_table *table;
    table = xkb_compose_table_new_from_buffer(ctx, buffer, strlen(buffer), "",
                                              XKB_COMPOSE_FORMAT_TEXT_V1,
                                              XKB_COMPOSE_COMPILE_NO_FLAGS);
    assert(table);
    va_start(ap, buffer);
    ok = test_compose_seq_va(table, ap);
    va_end(ap);
    xkb_compose_table_unref(table);
    return ok;
}

static void
test_seqs(struct xkb_context *ctx)
{
    struct xkb_compose_table *table;
    char *path;
    FILE *file;

    path = test_get_path("compose/en_US.UTF-8/Compose");
    file = fopen(path, "r");
    assert(file);
    free(path);

    table = xkb_compose_table_new_from_file(ctx, file, "",
                                            XKB_COMPOSE_FORMAT_TEXT_V1,
                                            XKB_COMPOSE_COMPILE_NO_FLAGS);
    assert(table);
    fclose(file);

    assert(test_compose_seq(table,
        XKB_KEY_dead_tilde,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_space,          XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "~",    XKB_KEY_asciitilde,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_dead_tilde,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_space,          XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "~",    XKB_KEY_asciitilde,
        XKB_KEY_dead_tilde,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_space,          XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "~",    XKB_KEY_asciitilde,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_dead_tilde,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_dead_tilde,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "~",    XKB_KEY_asciitilde,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_dead_acute,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_space,          XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "'",    XKB_KEY_apostrophe,
        XKB_KEY_Caps_Lock,      XKB_COMPOSE_FEED_IGNORED,   XKB_COMPOSE_COMPOSED,   "'",    XKB_KEY_apostrophe,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_dead_acute,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_dead_acute,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "´",    XKB_KEY_acute,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_Multi_key,      XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_Shift_L,        XKB_COMPOSE_FEED_IGNORED,   XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_Caps_Lock,      XKB_COMPOSE_FEED_IGNORED,   XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_Control_L,      XKB_COMPOSE_FEED_IGNORED,   XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_T,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "@",    XKB_KEY_at,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_7,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,    "",     XKB_KEY_NoSymbol,
        XKB_KEY_a,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,    "",     XKB_KEY_NoSymbol,
        XKB_KEY_b,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,    "",     XKB_KEY_NoSymbol,
        XKB_KEY_NoSymbol));

    assert(test_compose_seq(table,
        XKB_KEY_Multi_key,      XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_apostrophe,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_7,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_CANCELLED,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_7,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,    "",     XKB_KEY_NoSymbol,
        XKB_KEY_Caps_Lock,      XKB_COMPOSE_FEED_IGNORED,   XKB_COMPOSE_NOTHING,    "",     XKB_KEY_NoSymbol,
        XKB_KEY_NoSymbol));

    xkb_compose_table_unref(table);

    /* Make sure one-keysym sequences work. */
    assert(test_compose_seq_buffer(ctx,
        "<A>          :  \"foo\"  X \n"
        "<B> <A>      :  \"baz\"  Y \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "foo",   XKB_KEY_X,
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "foo",   XKB_KEY_X,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING, "",      XKB_KEY_NoSymbol,
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "baz",   XKB_KEY_Y,
        XKB_KEY_NoSymbol));

    /* No sequences at all. */
    assert(test_compose_seq_buffer(ctx,
        "",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_Multi_key,      XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_dead_acute,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_NoSymbol));

    /* Only keysym - string derived from keysym. */
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>     :  X \n"
        "<B> <A>     :  dollar \n"
        "<C>         :  dead_acute \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING, "",      XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "X",     XKB_KEY_X,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING, "",      XKB_KEY_NoSymbol,
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "$",     XKB_KEY_dollar,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "",      XKB_KEY_dead_acute,
        XKB_KEY_NoSymbol));

    /* Make sure a cancelling keysym doesn't start a new sequence. */
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>     :  X \n"
        "<C> <D>     :  Y \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING, "",      XKB_KEY_NoSymbol,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_CANCELLED, "",      XKB_KEY_NoSymbol,
        XKB_KEY_D,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,   "",      XKB_KEY_NoSymbol,
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING, "",      XKB_KEY_NoSymbol,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_CANCELLED, "",      XKB_KEY_NoSymbol,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING, "",      XKB_KEY_NoSymbol,
        XKB_KEY_D,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,  "Y",     XKB_KEY_Y,
        XKB_KEY_NoSymbol));
}

static void
test_conflicting(struct xkb_context *ctx)
{
    // new is prefix of old
    assert(test_compose_seq_buffer(ctx,
        "<A> <B> <C>  :  \"foo\"  A \n"
        "<A> <B>      :  \"bar\"  B \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "foo",  XKB_KEY_A,
        XKB_KEY_NoSymbol));

    // old is a prefix of new
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>      :  \"bar\"  B \n"
        "<A> <B> <C>  :  \"foo\"  A \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "foo",  XKB_KEY_A,
        XKB_KEY_NoSymbol));

    // new duplicate of old
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>      :  \"bar\"  B \n"
        "<A> <B>      :  \"bar\"  B \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "bar",  XKB_KEY_B,
        XKB_KEY_C,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_NOTHING,    "",     XKB_KEY_NoSymbol,
        XKB_KEY_NoSymbol));

    // new same length as old #1
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>      :  \"foo\"  A \n"
        "<A> <B>      :  \"bar\"  B \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "bar",  XKB_KEY_B,
        XKB_KEY_NoSymbol));

    // new same length as old #2
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>      :  \"foo\"  A \n"
        "<A> <B>      :  \"foo\"  B \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "foo",  XKB_KEY_B,
        XKB_KEY_NoSymbol));

    // new same length as old #3
    assert(test_compose_seq_buffer(ctx,
        "<A> <B>      :  \"foo\"  A \n"
        "<A> <B>      :  \"bar\"  A \n",
        XKB_KEY_A,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_B,              XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "bar",  XKB_KEY_A,
        XKB_KEY_NoSymbol));
}

static void
test_state(struct xkb_context *ctx)
{
    struct xkb_compose_table *table;
    struct xkb_compose_state *state;
    char *path;
    FILE *file;

    path = test_get_path("compose/en_US.UTF-8/Compose");
    file = fopen(path, "r");
    assert(file);
    free(path);

    table = xkb_compose_table_new_from_file(ctx, file, "",
                                            XKB_COMPOSE_FORMAT_TEXT_V1,
                                            XKB_COMPOSE_COMPILE_NO_FLAGS);
    assert(table);
    fclose(file);

    state = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
    assert(state);

    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);
    xkb_compose_state_reset(state);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);
    xkb_compose_state_feed(state, XKB_KEY_NoSymbol);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);
    xkb_compose_state_feed(state, XKB_KEY_Multi_key);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSING);
    xkb_compose_state_reset(state);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);
    xkb_compose_state_feed(state, XKB_KEY_Multi_key);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSING);
    xkb_compose_state_feed(state, XKB_KEY_Multi_key);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_CANCELLED);
    xkb_compose_state_feed(state, XKB_KEY_Multi_key);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSING);
    xkb_compose_state_feed(state, XKB_KEY_Multi_key);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_CANCELLED);
    xkb_compose_state_reset(state);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);
    xkb_compose_state_feed(state, XKB_KEY_dead_acute);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSING);
    xkb_compose_state_feed(state, XKB_KEY_A);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSED);
    xkb_compose_state_reset(state);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);
    xkb_compose_state_feed(state, XKB_KEY_dead_acute);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSING);
    xkb_compose_state_feed(state, XKB_KEY_A);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_COMPOSED);
    xkb_compose_state_reset(state);
    xkb_compose_state_feed(state, XKB_KEY_NoSymbol);
    assert(xkb_compose_state_get_status(state) == XKB_COMPOSE_NOTHING);

    xkb_compose_state_unref(state);
    xkb_compose_table_unref(table);
}

static void
test_XCOMPOSEFILE(struct xkb_context *ctx)
{
    struct xkb_compose_table *table;
    char *path;

    path = test_get_path("compose/en_US.UTF-8/Compose");
    setenv("XCOMPOSEFILE", path, 1);
    free(path);

    table = xkb_compose_table_new_from_locale(ctx, "blabla",
                                              XKB_COMPOSE_COMPILE_NO_FLAGS);
    assert(table);

    assert(test_compose_seq(table,
        XKB_KEY_dead_tilde,     XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSING,  "",     XKB_KEY_NoSymbol,
        XKB_KEY_space,          XKB_COMPOSE_FEED_ACCEPTED,  XKB_COMPOSE_COMPOSED,   "~",    XKB_KEY_asciitilde,
        XKB_KEY_NoSymbol));

    xkb_compose_table_unref(table);
}

int
main(int argc, char *argv[])
{
    struct xkb_context *ctx;

    ctx = test_get_context(CONTEXT_NO_FLAG);
    assert(ctx);

    if (argc > 1 && streq(argv[1], "bench")) {
        benchmark(ctx);
        xkb_context_unref(ctx);
        return 0;
    }

    test_seqs(ctx);
    test_conflicting(ctx);
    test_XCOMPOSEFILE(ctx);
    test_state(ctx);

    xkb_context_unref(ctx);
    return 0;
}
