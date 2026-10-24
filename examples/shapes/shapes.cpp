// shapes — a C++ example: classes, virtual functions, std::vector and std::string.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace geo {

class Shape {
public:
    explicit Shape(std::string name) : name_(std::move(name)) {}
    virtual ~Shape() {}
    virtual double area() const = 0;
    const std::string &name() const { return name_; }
private:
    std::string name_;
};

class Rect : public Shape {
public:
    Rect(double w, double h) : Shape("rectangle"), w_(w), h_(h) {}
    double area() const override { return w_ * h_; }
private:
    double w_, h_;
};

class Circle : public Shape {
public:
    explicit Circle(double r) : Shape("circle"), r_(r) {}
    double area() const override { return 3.14159265 * r_ * r_; }
private:
    double r_;
};

} // namespace geo

int main()
{
    std::vector<std::unique_ptr<geo::Shape>> shapes;
    shapes.push_back(std::make_unique<geo::Rect>(3.0, 4.0));
    shapes.push_back(std::make_unique<geo::Circle>(2.0));
    shapes.push_back(std::make_unique<geo::Rect>(1.5, 2.5));

    double total = 0;
    for (const auto &s : shapes) {
        double a = s->area();
        std::printf("%-10s %8.2f\n", s->name().c_str(), a);
        total += a;
    }
    std::printf("total area %.2f\n", total);
    return 0;
}
