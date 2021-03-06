/*******************************************************************************
Project       : HTTP Downloader using boost::asio
Version       : 1.0
Author        : Martin Christen, martin.christen@fhnw.ch
Copyright     : (c) 2006-2010 by FHNW/IVGI. All Rights Reserved

$License$
*******************************************************************************/

/* Header Field Definitions HTTP/1.1:
   http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html
   http://en.wikipedia.org/wiki/List_of_HTTP_header_fields
*/


#ifdef _MSC_VER
#define _WIN32_WINNT 0x0501
#endif

#include "Downloader.h"
#include <iostream>
#include <fstream>
#include <math.h>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/function/function0.hpp>


namespace fs = boost::filesystem;
using boost::asio::ip::tcp;

//------------------------------------------------------------------------------

downloadTask::downloadTask( std::string remoteUrl, std::string locationUrl, mpf callback, void* userData) 
   : m_blockCount(0), m_remoteUrl(remoteUrl), m_locationUrl(locationUrl), m_fileLength(0), m_downloaded(0), m_downRate(0), m_event(callback), m_userData(userData), m_isFinish(false)
{
	downloadTask::parseUrl( m_remoteUrl, m_hostName, m_fileName );
}

//------------------------------------------------------------------------------

downloadTask::~downloadTask()
{
}

//------------------------------------------------------------------------------

void downloadTask::parseUrl( std::string url, std::string &host, std::string &fileName )
{
	int n = url.find("http://");
	if(n != std::string::npos)
	{
		url = url.substr(n + std::string("http://").length(), -1);
	}
	n = url.find( "/" );
	host = url.substr(0, n);

	fileName = url.substr(n, -1);
}

//------------------------------------------------------------------------------
// Get File size using "Content-Length:"-Entry
unsigned long downloadTask::getFileLength()
{
	try
	{
		if( m_fileLength != 0 )
		{
			return m_fileLength;
		}

		unsigned long fileLength = 0;

		boost::asio::io_service io_service;
		tcp::resolver resolver(io_service);
		tcp::resolver::query query( m_hostName, "http" );
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;
		tcp::socket socket(io_service);
		boost::system::error_code error = boost::asio::error::host_not_found;

		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}

		if (error)
		{
      	throw boost::system::system_error(error);
      }

		boost::asio::streambuf request;
		std::ostream request_stream(&request);
		request_stream << "GET " << m_fileName << "" << " HTTP/1.1\r\n";
		request_stream << "Host: " << m_hostName << "\r\n";   // The domain name of the server (for virtual hosting), mandatory since HTTP/1.1
		request_stream << "Accept: */*\r\n";                  // Content-Types that are acceptable
		request_stream << "Connection: Close\r\n\r\n";        // What type of connection the user-agent would prefer
      
      boost::asio::write( socket, request );

		boost::asio::streambuf response;
		boost::asio::read_until( socket, response, "\r\n");
		std::istream response_stream(&response);

		std::string http_version;
		unsigned int status_code;
		std::string status_message;

		response_stream >> http_version;
		response_stream >> status_code;
		std::getline(response_stream, status_message);
		if (!response_stream || http_version.substr(0, 5) != "HTTP/" || status_code/100 != 2)
		{
			//socket.close();
			return 0;
		}
		boost::asio::read_until( socket, response, "\r\n\r\n" );
		std::string buff;
		while ( std::getline(response_stream, buff) )
		{
			int n = buff.find( "Content-Length: " );
			if( n != std::string::npos )
			{
				fileLength = atoi( buff.substr( std::string( "Content-Length=" ).length(), -1 ).c_str() );
				m_fileLength = fileLength;
				break;
			}
		}
		socket.close();
		return fileLength;
	}
	catch (std::exception& e)
	{
		std::cout << "Exception: " << e.what() << __LINE__ << "\n";
		return 0;
	}
}

//------------------------------------------------------------------------------
// download the data (synchronous)
bool downloadTask::download()
{
   _vHeader.clear();
	try
	{
		uintmax_t startRange = 0;
		if( fs::exists( m_locationUrl ) )
		{
			m_downloaded = fs::file_size( m_locationUrl );
			if( m_downloaded >= getFileLength() )
			{
				m_isFinish = true;
				return true;
			}
			startRange = m_downloaded;
		}

		boost::asio::io_service io_service;
		tcp::resolver resolver(io_service);
		tcp::resolver::query query( m_hostName, "http" );
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;
		tcp::socket socket(io_service);
		boost::system::error_code error = boost::asio::error::host_not_found;
		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}
		if (error)
			throw boost::system::system_error(error);


		boost::asio::streambuf request;
		std::ostream request_stream(&request);
		request_stream << "GET " << m_fileName << "" << " HTTP/1.1\r\n";
		request_stream << "Host: " << m_hostName << "\r\n";
		request_stream << "RANGE: bytes="<< startRange << "-" << "\r\n";
		request_stream << "Accept: */*\r\n";
		request_stream << "Connection: Close\r\n\r\n";
		int n = boost::asio::write( socket, request );

		boost::asio::streambuf header;
		std::istream header_stream(&header);
		boost::asio::read_until( socket, header, "\r\n\r\n" );
      
      _status_code = 0;


		std::string http_version;  
		std::string status_message;  
		std::string headBuff;
		header_stream >> http_version;  
		header_stream >> _status_code;

		//std::cout << http_version <<" " << _status_code << "\n"; 

      std::string first, second;

		while ( std::getline(header_stream, headBuff) && headBuff != "\r" )
		{
         first.clear();
         second.clear();
         int stat = 0;
			for (size_t i=0;i<headBuff.length();i++)
         {
            if (stat == 0)
            {
               if (headBuff[i] == ':')
               {
                 stat = 1;
               }
               else
               {
                  if (headBuff[i] >= 32)
                     first += headBuff[i];
               }
            }
            else
            {
              if (stat == 1)
              {
                  // ignore spaces
                  if (headBuff[i] != ' ')
                  {
                     stat = 2;
                     if (headBuff[i] >= 32)
                        second += headBuff[i];
                  }
              }
              else
              {   
                  if (headBuff[i] >= 32)
                     second += headBuff[i];
              }
            }
         }

         if (first.length()>0 && second.length()>0)
         {
            _vHeader.push_back(std::pair<std::string, std::string>(first, second));
         }
		}
		//std::cout<<endl;

		if( _status_code/100 != 2 || http_version.substr( 0, 5 ) != "HTTP/" )
		{
			throw boost::system::system_error( error, (boost::lexical_cast<std::string>(_status_code)).c_str() );
		}

		// Write File
		std::ofstream file;
		if( _status_code == 200 && m_downloaded > 0 )
		{
			//std::cout<<" HTTP status 200" << "\n";
			file.open( m_locationUrl.c_str(), std::ios::out|std::ios::ate|std::ios::binary );
		}
		else
		{
			file.open( m_locationUrl.c_str(), std::ios::app|std::ios::binary );
		}
		if( !file )
		{
			throw "open file failed";
		}
		
		// Merge File
		static unsigned long startTime = GetTickCount();
		unsigned long deltaDown = 0;
		unsigned long deltaTime = 0;
		unsigned long readNum = 0;
		while( true )
		{
			boost::asio::read( socket, header, boost::asio::transfer_at_least(1), error );
			readNum = header.size();
			if( readNum == 0 )
			{
				break;
			}
			m_downloaded += readNum;
			deltaDown += readNum;
			deltaTime = GetTickCount() - startTime;
			if( deltaTime >= 1000 )
			{
				startTime = GetTickCount();
				m_downRate = deltaDown;
				deltaDown = 0;
				alert( process );
			}
			file<<&header;
		}
		file.close();
		if (error != boost::asio::error::eof)
		{
			throw boost::system::system_error(error);
		}
		socket.close();
	}
	catch (std::exception& e)
	{
		std::cout << "Download failed: " << e.what() << "\n";
		return false;
	}
	m_isFinish = true;
	return true;
}

uintmax_t downloadTask::getDownloaded()
{
	return m_downloaded;
}

void downloadTask::alert( alertType type )
{
	if( m_event )
	{
		m_event( this, type, m_userData );
	}
}

unsigned long downloadTask::getDownRate()
{
	return m_downRate;
}

