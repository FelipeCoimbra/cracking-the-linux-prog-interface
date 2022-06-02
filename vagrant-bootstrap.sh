#!/bin/bash

sudo apt-get update

## Setup welcome message
sudo apt-get install -y cowsay
sudo echo -e "\necho \"This is my Vagrant environment for running exercises from The Linux Programming Interface in a Ubuntu Server 20.04\" | cowsay\n" >> /home/vagrant/.bashrc
sudo ln -s /usr/games/cowsay /usr/local/bin/cowsay

## Setup build tools
sudo apt-get install -y build-essential