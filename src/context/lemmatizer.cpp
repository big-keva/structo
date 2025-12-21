# include "../../context/lemmatizer.hpp"
# include <mtc/sharedLibrary.hpp>
# include <memory>

namespace structo {
namespace context {

  class DynaModule final: public ILemmatizer
  {
    implement_lifetime_control

  public:
    DynaModule( mtc::SharedLibrary, const char* );

  public:
    int   Lemmatize( IWord*, unsigned, const widechar*, size_t ) override;

  protected:
    std::shared_ptr<std::pair<mtc::SharedLibrary, mtc::api<ILemmatizer>>>
      holder;

  };

  // DynaModule implementation

  DynaModule::DynaModule( mtc::SharedLibrary libmod, const char* args )
  {
    auto  module = mtc::api<ILemmatizer>();
    auto  fnInit = (CreateLemmatizer)libmod.Find( "CreateLemmatizer" );
    int   nerror;

    if ( (nerror = fnInit( module, args )) != 0 )
      throw std::runtime_error( "Failed to create lemmatizer , error code: " + std::to_string( nerror ) );

    holder = std::make_shared<std::pair<mtc::SharedLibrary, mtc::api<ILemmatizer>>>(
      libmod, module );
  }

  int   DynaModule::Lemmatize( IWord* lemmas, unsigned uflags, const widechar* pwsstr, size_t cchstr )
  {
    return holder != nullptr ? holder->second->Lemmatize( lemmas, uflags, pwsstr, cchstr ) : EFAULT;
  }

  auto  LoadLemmatizer( const char* path, const char* args ) -> mtc::api<ILemmatizer>
  {
    auto  module = mtc::SharedLibrary::Load( path );

    return new DynaModule( module, args );
  }

  auto  LoadLemmatizer( const std::string& path, const std::string& args ) -> mtc::api<ILemmatizer>
  {
    return LoadLemmatizer( path.c_str(), args.c_str() );
  }

}}
