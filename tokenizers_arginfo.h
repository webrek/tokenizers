/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: b0e5a212f0967105170b4d13ea403229b4a3741c */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_version, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_cache_count, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_encode, 0, 2, IS_ARRAY, 0)
	ZEND_ARG_OBJ_INFO(0, t, Tokenizers\\Bpe, 0)
	ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, allowedSpecial, IS_ARRAY, 0, "[]")
	ZEND_ARG_TYPE_MASK(0, disallowedSpecial, MAY_BE_ARRAY|MAY_BE_STRING, "\"all\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_decode, 0, 2, IS_STRING, 0)
	ZEND_ARG_OBJ_INFO(0, t, Tokenizers\\Bpe, 0)
	ZEND_ARG_TYPE_INFO(0, ids, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_count, 0, 2, IS_LONG, 0)
	ZEND_ARG_OBJ_INFO(0, t, Tokenizers\\Bpe, 0)
	ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Tokenizers_Bpe_fromTiktokenFile, 0, 2, Tokenizers\\Bpe, 0)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, pattern, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, specialTokens, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Tokenizers_Bpe_fromVocab, 0, 3, Tokenizers\\Bpe, 0)
	ZEND_ARG_TYPE_INFO(0, tokenBytesToId, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, merges, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, pattern, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, specialTokens, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Tokenizers_Bpe_encode, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
	ZEND_ARG_TYPE_MASK(0, allowedSpecial, MAY_BE_ARRAY|MAY_BE_STRING, "[]")
	ZEND_ARG_TYPE_MASK(0, disallowedSpecial, MAY_BE_ARRAY|MAY_BE_STRING, "\"all\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Tokenizers_Bpe_countTokens, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Tokenizers_Bpe_decode, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, ids, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Tokenizers_Bpe_decodeSingle, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, id, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Tokenizers_Bpe_vocabSize arginfo_tokenizers_cache_count

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Tokenizers_Bpe_name, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()


ZEND_FUNCTION(tokenizers_version);
ZEND_FUNCTION(tokenizers_cache_count);
ZEND_FUNCTION(tokenizers_encode);
ZEND_FUNCTION(tokenizers_decode);
ZEND_FUNCTION(tokenizers_count);
ZEND_METHOD(Tokenizers_Bpe, fromTiktokenFile);
ZEND_METHOD(Tokenizers_Bpe, fromVocab);
ZEND_METHOD(Tokenizers_Bpe, encode);
ZEND_METHOD(Tokenizers_Bpe, countTokens);
ZEND_METHOD(Tokenizers_Bpe, decode);
ZEND_METHOD(Tokenizers_Bpe, decodeSingle);
ZEND_METHOD(Tokenizers_Bpe, vocabSize);
ZEND_METHOD(Tokenizers_Bpe, name);


static const zend_function_entry ext_functions[] = {
	ZEND_FE(tokenizers_version, arginfo_tokenizers_version)
	ZEND_FE(tokenizers_cache_count, arginfo_tokenizers_cache_count)
	ZEND_FE(tokenizers_encode, arginfo_tokenizers_encode)
	ZEND_FE(tokenizers_decode, arginfo_tokenizers_decode)
	ZEND_FE(tokenizers_count, arginfo_tokenizers_count)
	ZEND_FE_END
};


static const zend_function_entry class_Tokenizers_TokenizerException_methods[] = {
	ZEND_FE_END
};


static const zend_function_entry class_Tokenizers_Bpe_methods[] = {
	ZEND_ME(Tokenizers_Bpe, fromTiktokenFile, arginfo_class_Tokenizers_Bpe_fromTiktokenFile, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Tokenizers_Bpe, fromVocab, arginfo_class_Tokenizers_Bpe_fromVocab, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Tokenizers_Bpe, encode, arginfo_class_Tokenizers_Bpe_encode, ZEND_ACC_PUBLIC)
	ZEND_ME(Tokenizers_Bpe, countTokens, arginfo_class_Tokenizers_Bpe_countTokens, ZEND_ACC_PUBLIC)
	ZEND_ME(Tokenizers_Bpe, decode, arginfo_class_Tokenizers_Bpe_decode, ZEND_ACC_PUBLIC)
	ZEND_ME(Tokenizers_Bpe, decodeSingle, arginfo_class_Tokenizers_Bpe_decodeSingle, ZEND_ACC_PUBLIC)
	ZEND_ME(Tokenizers_Bpe, vocabSize, arginfo_class_Tokenizers_Bpe_vocabSize, ZEND_ACC_PUBLIC)
	ZEND_ME(Tokenizers_Bpe, name, arginfo_class_Tokenizers_Bpe_name, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static void register_tokenizers_symbols(int module_number)
{
	REGISTER_STRING_CONSTANT("Tokenizers\\VERSION", "0.1.0", CONST_PERSISTENT);
}

static zend_class_entry *register_class_Tokenizers_TokenizerException(zend_class_entry *class_entry_RuntimeException)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Tokenizers", "TokenizerException", class_Tokenizers_TokenizerException_methods);
	class_entry = zend_register_internal_class_ex(&ce, class_entry_RuntimeException);

	return class_entry;
}

static zend_class_entry *register_class_Tokenizers_Bpe(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Tokenizers", "Bpe", class_Tokenizers_Bpe_methods);
	class_entry = zend_register_internal_class_ex(&ce, NULL);
	class_entry->ce_flags |= ZEND_ACC_FINAL;

	return class_entry;
}
