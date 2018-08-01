#include <pybind11/pybind11.h>

namespace py = pybind11;

class socket_wrapper
{
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
        try
        {
            src.attr("fileno");
        }
        catch (std::exception)
        {
            return false;
        }
        return true;
    }
};

}} // namespace pybind11::detail

PYBIND11_MODULE(hangup, m)
{
    m.def("foo", [](const socket_wrapper &x) {});
    m.def("foo", [](const std::string &x) {});
}
