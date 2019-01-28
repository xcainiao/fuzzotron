import sys
import random



def flag():
     data = "\xFF\x00\x00\x00"
     data = data + "\x00\x00\x00\x00\x00"
     data = data + "\x01\x01\x00"
     data = data + "\x02"
     data = data + "\xff\xff\xff\xff\xff\xff\xff\xff"
     return data

def random_data(length) :
    data = b''
    for data_index in range(length) :
        data += chr(random.randrange(255))
    return data

filename = sys.argv[1]

with open(filename, "w") as f:
    
    """
    data = flag()
    f.write(data)
    """

    length = random.randint(10000,20000)
    data = random_data(length)
    f.write(data)
