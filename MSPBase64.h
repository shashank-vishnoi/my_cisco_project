/**
\addtogroup gslProxy
@{
  \file mspBase64.h

  \brief header file for Encoding/Decoding base-64 data between HTTP Client and 3rd Party Server.

  \n Original author: Nilesh Dixit
  \n Version 0.1 2011-01-25 (Nilesh Dixit)

  \n Copyright 2009-2010 Cisco Systems, Inc.
  \defgroup Signaling
@}*/

#include <string>
#include <iostream>

std::string mspEncode(unsigned char const* , unsigned int length);
std::string mspDecode(std::string const& s);

static const std::string mspBase =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";
