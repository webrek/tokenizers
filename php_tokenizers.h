#ifndef PHP_TOKENIZERS_H
#define PHP_TOKENIZERS_H

extern zend_module_entry tokenizers_module_entry;
#define phpext_tokenizers_ptr &tokenizers_module_entry

#define PHP_TOKENIZERS_VERSION "0.1.0"

#if defined(ZTS) && defined(COMPILE_DL_TOKENIZERS)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif /* PHP_TOKENIZERS_H */
