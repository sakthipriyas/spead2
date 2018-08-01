#include <pybind11/pybind11.h>

namespace py = pybind11;

class socket_wrapper
{
    int fd;

public:
    socket_wrapper() : fd(-1) {}
    explicit socket_wrapper(int fd) : fd(fd) {}
};

namespace pybind11
{
namespace detail
{

template<>
struct type_caster<socket_wrapper>
{
public:
    PYBIND11_TYPE_CASTER(socket_wrapper, _("socket.socket"));

    bool load(handle src, bool)
    {
        int fd = 1;
        try
        {
            src.attr("fileno")();
        }
        catch (std::exception)
        {
            return false;
        }
        value = socket_wrapper(fd);
        return true;
    }
};

}} // namespace pybind11::detail

PYBIND11_MODULE(hangup, m)
{
    m.def("foo", [](const socket_wrapper &x) {});
    m.def("foo", [](const std::string &x) {});
}
