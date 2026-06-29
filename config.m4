PHP_ARG_ENABLE([tokenizers],
  [whether to enable tokenizers support],
  [AS_HELP_STRING([--enable-tokenizers], [Enable tokenizers])], [no])

if test "$PHP_TOKENIZERS" != "no"; then
  AC_DEFINE(HAVE_TOKENIZERS, 1, [ Have tokenizers ])

  dnl Locate PCRE2 headers and library (used by src/pretok.c for pre-tokenization)
  AC_MSG_CHECKING([for PCRE2 via pcre2-config])
  PCRE2_PREFIX=`pcre2-config --prefix 2>/dev/null`
  if test -n "$PCRE2_PREFIX" && test -f "$PCRE2_PREFIX/include/pcre2.h"; then
    PCRE2_INC="$PCRE2_PREFIX/include"
    PCRE2_LIB="$PCRE2_PREFIX/lib"
    AC_MSG_RESULT([$PCRE2_PREFIX])
  else
    PCRE2_INC="/opt/homebrew/include"
    PCRE2_LIB="/opt/homebrew/lib"
    AC_MSG_RESULT([fallback to /opt/homebrew])
  fi
  PHP_ADD_INCLUDE($PCRE2_INC)
  PHP_ADD_LIBRARY_WITH_PATH(pcre2-8, $PCRE2_LIB, TOKENIZERS_SHARED_LIBADD)
  PHP_SUBST(TOKENIZERS_SHARED_LIBADD)

  PHP_NEW_EXTENSION(tokenizers,
    [tokenizers.c src/model.c src/base64.c src/heap.c src/engine.c src/pretok.c src/loader_tiktoken.c src/cache.c src/bpe_class.c src/normalize.c src/wordpiece.c src/wp_class.c src/unigram.c],
    $ext_shared,, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
  PHP_ADD_EXTENSION_DEP(tokenizers, json)
  PHP_ADD_EXTENSION_DEP(tokenizers, pcre)
fi
