use std::ffi::c_char;

#[unsafe(no_mangle)]
pub extern "C" fn xenon_ui_main(_argc: i32, _argv: *const *const c_char) -> i32 {
    0
}
