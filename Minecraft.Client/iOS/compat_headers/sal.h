// Shim for Microsoft's sal.h (Source Annotation Language).
//
// Upstream code uses these annotations decoratively; none of them have any
// semantic effect outside of the MSVC static analyzer. We expand them all to
// nothing so the code compiles under clang on iOS.

#pragma once

#ifndef _In_
#  define _In_
#endif
#ifndef _In_opt_
#  define _In_opt_
#endif
#ifndef _Out_
#  define _Out_
#endif
#ifndef _Out_opt_
#  define _Out_opt_
#endif
#ifndef _Inout_
#  define _Inout_
#endif
#ifndef _Inout_opt_
#  define _Inout_opt_
#endif
#ifndef _In_z_
#  define _In_z_
#endif
#ifndef _Out_z_
#  define _Out_z_
#endif
#ifndef _Printf_format_string_
#  define _Printf_format_string_
#endif
#ifndef _In_reads_
#  define _In_reads_(x)
#endif
#ifndef _Out_writes_
#  define _Out_writes_(x)
#endif
#ifndef _In_range_
#  define _In_range_(lo, hi)
#endif
#ifndef _Ret_maybenull_
#  define _Ret_maybenull_
#endif
#ifndef _Ret_notnull_
#  define _Ret_notnull_
#endif
#ifndef _Ret_
#  define _Ret_
#endif
#ifndef _When_
#  define _When_(cond, ann)
#endif
#ifndef _Success_
#  define _Success_(expr)
#endif
#ifndef _Check_return_
#  define _Check_return_
#endif
#ifndef _Must_inspect_result_
#  define _Must_inspect_result_
#endif
#ifndef _Notnull_
#  define _Notnull_
#endif
#ifndef _Null_
#  define _Null_
#endif
#ifndef _Pre_
#  define _Pre_
#endif
#ifndef _Post_
#  define _Post_
#endif
#ifndef _Frees_ptr_opt_
#  define _Frees_ptr_opt_
#endif
#ifndef _Frees_ptr_
#  define _Frees_ptr_
#endif
#ifndef _Deref_pre_
#  define _Deref_pre_
#endif
#ifndef _Deref_post_
#  define _Deref_post_
#endif
#ifndef _Analysis_noreturn_
#  define _Analysis_noreturn_
#endif
