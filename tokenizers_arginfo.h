/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 3874890476126d03ae4cedb96d3e9c649e707bf3 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_version, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()


ZEND_FUNCTION(tokenizers_version);


static const zend_function_entry ext_functions[] = {
	ZEND_FE(tokenizers_version, arginfo_tokenizers_version)
	ZEND_FE_END
};

static void register_tokenizers_symbols(int module_number)
{
	REGISTER_STRING_CONSTANT("Tokenizers\\VERSION", "0.1.0", CONST_PERSISTENT);
}
