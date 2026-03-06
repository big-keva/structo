# if !defined( __structo_context_x_contents_hpp__ )
# define __structo_context_x_contents_hpp__
# include "../contents.hpp"
# include "text-image.hpp"
# include "fields-man.hpp"

namespace structo {
namespace context {

  class Contents
  {
    friend auto  GetMiniContents(
      const mtc::span<const mtc::span<const Lexeme>>&,
      const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;
    friend auto  GetBM25Contents(
      const mtc::span<const mtc::span<const Lexeme>>&,
      const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;
    friend auto  GetRichContents(
      const mtc::span<const mtc::span<const Lexeme>>&,
      const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;

  public:
    struct impl;

    operator mtc::span<const EntryView>() const
      {  return get();  }
    auto get() const -> mtc::span<const EntryView>;

  protected:
    std::shared_ptr<impl> contents;

  };

  auto  GetMiniContents(
    const mtc::span<const mtc::span<const Lexeme>>&,
    const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;
  auto  GetBM25Contents(
    const mtc::span<const mtc::span<const Lexeme>>&,
    const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;
  auto  GetRichContents(
    const mtc::span<const mtc::span<const Lexeme>>&,
    const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;

  template <class Allocator>
  auto  MiniContents( const BaseImage<Allocator>& image ) -> Contents
  {
    extern FieldHandler& GetStubFields();

    return GetMiniContents( image.GetLemmas(), image.GetMarkup(), GetStubFields() );
  }
  template <class Allocator>
  auto  BM25Contents( const BaseImage<Allocator>& image ) -> Contents
  {
    extern FieldHandler& GetStubFields();

    return GetBM25Contents( image.GetLemmas(), image.GetMarkup(), GetStubFields() );
  }
  template <class Allocator>
  auto  RichContents( const BaseImage<Allocator>& image, FieldHandler& fdset ) -> Contents
  {
    return GetRichContents( image.GetLemmas(), image.GetMarkup(), fdset );
  }

}}

# endif   // !__structo_context_x_contents_hpp__
