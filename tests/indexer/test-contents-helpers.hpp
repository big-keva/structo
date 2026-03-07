# if !defined( __structo_tests_test_contents_helpers__ )
# define __structo_tests_test_contents_helpers__
# include "../../contents.hpp"
# include <mtc/zmap.h>
# include <map>

inline
auto  MapZval( const mtc::zval& zv ) -> std::string_view
{
  switch ( zv.get_type() )
  {
    case mtc::zval::z_type::z_char:         return { (const char*)zv.get_char(), 1 };
    case mtc::zval::z_type::z_byte:         return { (const char*)zv.get_byte(), 1 };
    case mtc::zval::z_type::z_int16:        return { (const char*)zv.get_int16(), sizeof(int16_t) };
    case mtc::zval::z_type::z_word16:       return { (const char*)zv.get_word16(), sizeof(uint16_t) };
    case mtc::zval::z_type::z_int32:        return { (const char*)zv.get_int32(), sizeof(int32_t) };
    case mtc::zval::z_type::z_word32:       return { (const char*)zv.get_word32(), sizeof(uint32_t) };
    case mtc::zval::z_type::z_int64:        return { (const char*)zv.get_int64(), sizeof(int64_t) };
    case mtc::zval::z_type::z_word64:       return { (const char*)zv.get_word64(), sizeof(uint64_t) };
    case mtc::zval::z_type::z_float:        return { (const char*)zv.get_float(), sizeof(float) };
    case mtc::zval::z_type::z_double:       return { (const char*)zv.get_double(), sizeof(double) };
    case mtc::zval::z_type::z_charstr:      return { (const char*)zv.get_charstr()->c_str(), zv.get_charstr()->length() };
    case mtc::zval::z_type::z_widestr:      return { (const char*)zv.get_widestr()->c_str(), sizeof(widechar) * zv.get_widestr()->length() };
    default:  throw std::logic_error( "unexpected zval type" );
  }
}

inline
auto  GetView( const std::map<const char*, mtc::zval>& ix ) -> std::vector<structo::EntryView>
{
  std::vector<structo::EntryView>  aviews;

  for ( auto& kv: ix )
    aviews.emplace_back( std::string_view( kv.first ), MapZval( kv.second ), 0 );

  return aviews;
}

# endif   // !__structo_tests_test_contents_helpers__
