PHP_ARG_ENABLE([tokenizers],
  [whether to enable tokenizers support],
  [AS_HELP_STRING([--enable-tokenizers], [Enable tokenizers])], [no])

if test "$PHP_TOKENIZERS" != "no"; then
  AC_DEFINE(HAVE_TOKENIZERS, 1, [ Have tokenizers ])
  PHP_NEW_EXTENSION(tokenizers,
    [tokenizers.c src/model.c src/base64.c src/heap.c src/engine.c src/loader_tiktoken.c src/cache.c src/bpe_class.c],
    $ext_shared,, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
  PHP_ADD_EXTENSION_DEP(tokenizers, json)
  PHP_ADD_EXTENSION_DEP(tokenizers, pcre)
fi
