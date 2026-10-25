struct Shape {
    virtual ~Shape();
    virtual int area() const = 0;
};

struct Square : Shape {
    int side;
    explicit Square(int s);
    int area() const override;
    void grow();
};
