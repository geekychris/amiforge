// version 1 (built into hpcpp)
#include "shape.h"

Shape::~Shape() {}

Square::Square(int s) : side(s) {}

int Square::area() const
{
    return side * side;
}

void Square::grow() { side++; }
