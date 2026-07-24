
#ifdef SWIGGO
%{
#include <memory>
%}

%ignore std::shared_ptr::shared_ptr;

namespace std {

  template<class T>
  class shared_ptr {
  public:
    T* operator->() const noexcept;
    T* get() const noexcept;

    long use_count() const noexcept;
  };


  template<class T>
  class enable_shared_from_this {
    protected:
      enable_shared_from_this() = default;
      ~enable_shared_from_this() = default;
    public:
      std::shared_ptr<T> shared_from_this();
  };

}

%define %shared_ptr(GO_SHARED_PTR_INTERFACE_TYPE, CPP_UNDERLYING_TYPE)

%template(##GO_SHARED_PTR_INTERFACE_TYPE) std::shared_ptr<##CPP_UNDERLYING_TYPE>;

%go_import("runtime")
%typemap(goout, noblock=1) std::shared_ptr<##CPP_UNDERLYING_TYPE> {
    runtime.SetFinalizer(&$result, func(swigptr_##GO_SHARED_PTR_INTERFACE_TYPE *##GO_SHARED_PTR_INTERFACE_TYPE) {
        Delete##GO_SHARED_PTR_INTERFACE_TYPE(*swigptr_##GO_SHARED_PTR_INTERFACE_TYPE)
    })
    $result = $1
}

%enddef


%define %enable_shared_from_this(GO_SHARED_PTR_INTERFACE_TYPE, CPP_UNDERLYING_TYPE)

%template(##GO_SHARED_PTR_INTERFACE_TYPE) std::enable_shared_from_this<##CPP_UNDERLYING_TYPE>;

%enddef

#else
%include <std_shared_ptr.i>
#endif
