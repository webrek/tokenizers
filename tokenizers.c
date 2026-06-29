#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "php.h"
#include "ext/standard/info.h"
#include "php_tokenizers.h"
#include "tokenizers_arginfo.h"
#include "src/cache.h"

extern void tk_register_bpe_class(void);
extern void tk_register_wp_class(void);

PHP_FUNCTION(tokenizers_version)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_STRING(PHP_TOKENIZERS_VERSION);
}

PHP_MINFO_FUNCTION(tokenizers)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "tokenizers support", "enabled");
    php_info_print_table_row(2, "version", PHP_TOKENIZERS_VERSION);
    php_info_print_table_end();
}

PHP_MINIT_FUNCTION(tokenizers)
{
    register_tokenizers_symbols(module_number);
    tk_cache_init();
    tk_register_bpe_class();
    tk_register_wp_class();
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(tokenizers) { tk_cache_shutdown(); return SUCCESS; }

zend_module_entry tokenizers_module_entry = {
    STANDARD_MODULE_HEADER,
    "tokenizers",
    ext_functions,            /* from arginfo */
    PHP_MINIT(tokenizers),
    PHP_MSHUTDOWN(tokenizers), NULL, NULL,
    PHP_MINFO(tokenizers),
    PHP_TOKENIZERS_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_TOKENIZERS
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(tokenizers)
#endif
