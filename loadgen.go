package main

import (
	"crypto/rand"
	"log"
	"net"
	"reflect"
)

func check(ctx string, err error) {
	if err != nil {
		log.Fatal(ctx, err)
	}
}

func genload(w chan bool) {
	conn, err := net.Dial("tcp", "prithvi:8200")
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
	w <- true
}

func main() {
	w := make(chan bool)
	for k := 0; k < 1000; k++ {
		go genload(w)
	}
	for k := 0; k < 1000; k++ {
		<-w
	}
}
