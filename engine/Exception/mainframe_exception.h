#ifndef MAINFRAME_EXCEPTION_H_INCLUDED
#define MAINFRAME_EXCEPTION_H_INCLUDED

namespace Exception ::mainframeExceptions {
    class handler_not_specified : public mainframeException {
    public:
        const char *what() const noexcept override {
            return "The handler of a main class is not specified.\n";
        }
    };

    class constant_pointer_changed : public mainframeException {
    public:
        const char *what() const noexcept override {
            return "A constant pointer is changed.\n";
        }
    };

} // namespace Exception::mainframeExceptions

#endif // MAINFRAME_EXCEPTION_H_INCLUDED
