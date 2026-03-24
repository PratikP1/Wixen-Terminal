/// Actions emitted by the VT parser after processing input bytes.
#[derive(Debug, Clone)]
pub enum Action {
    /// Print a visible character to the grid
    Print(char),
    /// Execute a C0/C1 control code
    Execute(u8),
    /// CSI dispatch: control sequence with parameters.
    /// `subparams` is parallel to `params`: each entry holds the colon-delimited
    /// sub-parameter values for the corresponding param (empty if none).
    CsiDispatch {
        params: Vec<u16>,
        subparams: Vec<Vec<u16>>,
        intermediates: Vec<u8>,
        action: char,
    },
    /// OSC dispatch: operating system command
    OscDispatch(Vec<Vec<u8>>),
    /// ESC dispatch
    EscDispatch {
        intermediates: Vec<u8>,
        action: char,
    },
    /// DCS hook
    DcsHook {
        params: Vec<u16>,
        intermediates: Vec<u8>,
        action: char,
    },
    /// DCS data put
    DcsPut(u8),
    /// DCS unhook
    DcsUnhook,
    /// APC dispatch: application program command payload (e.g. Kitty graphics protocol)
    ApcDispatch(Vec<u8>),
}
