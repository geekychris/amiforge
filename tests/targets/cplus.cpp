// Debuggee: C++ class in a namespace, std::string, overloads, a method call.
#include <string>
#include <cstdio>

namespace shapes {

class Rect {
public:
    Rect(int w, int h) : w_(w), h_(h), label_("rect") {}
    int area() const {
        return w_ * h_;             // @area_body
    }
    void grow(int by) { w_ += by; h_ += by; }
    const std::string &label() const { return label_; }
private:
    int w_, h_;
    std::string label_;
};

int measure(const Rect &r) { return r.area(); }
int measure(int a, int b) { return a * b; }

}  // namespace shapes

int main()
{
    shapes::Rect r(3, 4);
    std::string s = "abc";
    s += r.label();                 // @main_concat
    r.grow(1);
    int a = shapes::measure(r);     // @main_measure
    int b = shapes::measure(2, 3);
    std::printf("%s %d %d\n", s.c_str(), a, b);
    return (a + b) & 0x3F;
}
