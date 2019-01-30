import sys
import random



def flag():
    func = random.choice ( ['\x01', '\x04', '\x06', '\x07', '\x0a', '\x0c', '\x20'] )
    data = "\x0e"
    data = data + func
    data = data + "\x2b\x2b\x2b"
    data = data + "\x2b\x2b\x2b"
    data = data + "\x00\x00"
    b1 = random.choice ( ['\x01', '\x02', '\x03'] ) 
    b2 = random.choice ( ['\x11', '\x22', '\x33', '\x12', '\x23', '\x13', '\x0a', '\x21', '\x15'] ) 
    data = data +  b1 + b2
    length = ord(b1)<<8|ord(b2)
    return data, length

def random_data(length) :
    data = b''
    for data_index in range(length) :
        data += chr(random.randrange(255))
    return data

filename = sys.argv[1]

with open(filename, "w") as f:
    
    data, length = flag()
    f.write(data)

    data = random_data( ((length+30)&0xfffc) )
    f.write(data)
