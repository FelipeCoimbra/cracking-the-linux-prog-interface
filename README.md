# cracking-the-linux-prog-interface
My solutions to The Linux Programming Interface.

I've tested them on Ubuntu 20.04 only.

Some extra investigations of my own interest that I might do:
* [ ] **Chpt 3**: Benchmark of system calls' overheads for common calls

## Environment

I have set a Vagrant environment to make it easier to run these programs, as I don't use Ubuntu. You'll need to install [Vagrant](https://www.vagrantup.com/) and a VM provider, 
I personally like the simplicity of [VirtualBox](https://www.virtualbox.org/).

For OS X users, you can probably get away with a `brew install vagrant` and `brew install virtualbox` like I've done.

1. Start the VM with `vagrant up`.
2. I've configured the VM in a `public_network` for simplicity. Pick the network interface being used to access the internet for Vagrant to use as a bridge.
3. Access the VM with `vagrant ssh`.

## Running C solutions

The `c/` folder has solutions written in C.

Build them with `make` and run them with `./run CHAPTER QUESTION [...ARGS]`.

If you're not sure how to call a specific question, look at the source, it's _eAsY_.

Chapter 5, question 1 -> `c/chpt5/q1.c`. Look for function `q1`.