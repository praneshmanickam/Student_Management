# ======================================================================
#  Student Record Management System Using B-Tree
#  Makefile - builds the C backend and the test suite
# ======================================================================

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
LDLIBS  ?= -lpthread

SRC     := backend/main.c backend/btree.c backend/student.c \
           backend/storage.c backend/server.c
OBJ     := $(SRC:.c=.o)
TARGET  := student_server

TEST_SRC    := backend/btree.c backend/student.c backend/storage.c test/btree_test.c
TEST_TARGET := btree_test


.PHONY: all clean test run seed test-frontend test-all

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDLIBS)
	@echo "Built $(TARGET) - run it with: ./$(TARGET)"

# Windows (MinGW):  gcc -Wall -Wextra -O2 backend\*.c -o student_server.exe -lws2_32
$(TEST_TARGET): $(TEST_SRC)
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) $(LDLIBS)
	@echo "Built $(TEST_TARGET)"

test: $(TEST_TARGET)
	./$(TEST_TARGET)

test-frontend:
	node test/frontend_test.js

test-all: test test-frontend
	bash test/integration_test.sh

run: $(TARGET)
	./$(TARGET)

seed: $(TARGET)
	./$(TARGET) --seed

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJ) backend/*.o
