package main

import (
	"crypto/rand"
	"log"
	"net"
	"os"
	"reflect"
)

func check(ctx string, err error) {
	if err != nil {
		log.Fatal(ctx, err)
	}
}

func genload(address string, done chan bool) {
	conn, err := net.Dial("tcp", address)
	check("dial", err)
	defer conn.Close()
	data := make([]byte, 1024)
	rand.Read(data)
	buf := make([]byte, 1024)
	for k := 0; k < 1000; k++ {
		var nw, nr int
		var err error
		nw, err = conn.Write(data)
		check("write", err)
		nr, err = conn.Read(buf)
		check("read", err)
		if nw != nr {
			log.Fatal("nr != nw")
		}
		if !reflect.DeepEqual(buf, data) {
			log.Fatal("data mismatch")
		}
	}
	done <- true
}

func main() {
	if len(os.Args) != 2 {
		log.Fatal("Usage: loadgen host:port")
	}
	done := make(chan bool)
	for k := 0; k < 1000; k++ {
		go genload(os.Args[1], done)
	}
	for k := 0; k < 1000; k++ {
		<-done
	}
}
