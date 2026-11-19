#include "stdafx.h"
#ifdef _MSC_VER
#include <xhash>
#define DO_HASH(s) hash_value(s)
#else
#include <functional>
#define DO_HASH(s) (std::hash<std::wstring>()(s))
#endif

#include "Hasher.h"

Hasher::Hasher(wstring &salt)
{
	this->salt = salt;
}

wstring Hasher::getHash(wstring &name)
{
	// 4J Stu - Removed try/catch
	//try {
		wstring s = wstring( salt ).append( name );
		//MessageDigest m;
		//m = MessageDigest.getInstance("MD5");
		//m.update(s.getBytes(), 0, s.length());
		//return new BigInteger(1, m.digest()).toString(16);

		// TODO 4J Stu - Will this hash us with the same distribution as the MD5?
		return _toString( DO_HASH( s ) );
	//}
	//catch (NoSuchAlgorithmException e)
	//{
	//	throw new RuntimeException(e);
	//}
}