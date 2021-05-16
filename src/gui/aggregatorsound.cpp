/*	
	This file is part of Ingnomia https://github.com/rschurade/Ingnomia
	Copyright (C) 2017-2020  Ralph Schurade, Ingnomia Team

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "aggregatorsound.h"

#include "../base/config.h"
#include "../base/db.h"
#include "../base/gamestate.h"
#include "../base/position.h"
#include "../game/game.h"
#include "../game/soundmanager.h"
#include "../gui/eventconnector.h"
#include "../gui/mainwindowrenderer.h"

#include <QDebug>
#include <QJsonDocument>

#include <SFML/Audio.hpp>

AggregatorSound::AggregatorSound( QObject* parent ) :
	QObject( parent )
{
	//sf::SoundBuffer buffer;
	QString exePath = QCoreApplication::applicationDirPath();

	connect( Global::eventConnector, &EventConnector::signalCameraPosition, this, &AggregatorSound::onCameraPosition );
}

AggregatorSound::~AggregatorSound()
{
}

void AggregatorSound::init( Game* game )
{
	g = game;

	setVolume( Global::cfg->get( "AudioMasterVolume" ).toFloat() );
	QList<QVariantMap> soundList = DB::selectRows( "Sounds" );
	for ( auto& sound : soundList )
	{
		QString soundID  = sound.value( "ID" ).toString() + "." + sound.value( "Material" ).toString();
		QString filename = sound.value( "SoundFile" ).toString();
		sf::SoundBuffer buffer;
		QString exePath = QCoreApplication::applicationDirPath();
		filename        = exePath + "/content/audio/" + filename;

		if ( !buffer.loadFromFile( filename.toStdString() ) )
		{
			qDebug() << "unable to load sound" << soundID << " " << filename;
		}
		else
		{
			qDebug() << "loaded sound " << soundID << " " << filename;
			m_buffers.insert( soundID, std::move( buffer ) );
		}
	}
}

void AggregatorSound::onPlayEffect( const SoundEffect& effectRequest )
{
	garbageCollection();

	QString soundID       = effectRequest.type + ".";
	QString soundMaterial = effectRequest.material;
	if ( m_buffers.contains( soundID ) || m_buffers.contains( soundID + soundMaterial ) )
	{
		if ( m_buffers.contains( soundID + soundMaterial ) )
		{
			soundID = soundID + soundMaterial;
		}
		else
		{
			QString mat = soundID + soundMaterial;
			if ( Global::debugSound )
			{
				qDebug() << "Unknown sound material " << mat;
			}
		}
		m_activeEffects.append( ActiveEffect {
			false,
			effectRequest.origin,
			sf::Sound( m_buffers[soundID] ) } );
		auto& sound = m_activeEffects.back().sound;
		sound.setPosition( effectRequest.origin.x, effectRequest.origin.y, effectRequest.origin.z );
		sound.setRelativeToListener( false );
		rebalanceSound( m_activeEffects.back() );
		sound.play();

		if ( Global::debugSound )
		{
			qDebug() << "playing sound " << soundID;
		}
	}
	else
	{
		if ( Global::debugSound )
		{
			qDebug() << "Unknown sound " << soundID;
		}
	}
}

void AggregatorSound::onPlayNotify( const SoundEffect& effectRequest )
{
	garbageCollection();

	QString soundID       = effectRequest.type + ".";
	QString soundMaterial = effectRequest.material;
	if ( m_buffers.contains( soundID ) || m_buffers.contains( soundID + soundMaterial ) )
	{
		if ( m_buffers.contains( soundID + soundMaterial ) )
		{
			soundID = soundID + soundMaterial;
		}
		else
		{

			if ( Global::debugSound )
			{
				QString mat = soundID + soundMaterial;
				qDebug() << "Unknown sound material " << mat;
			}
		}
		m_activeEffects.append( ActiveEffect {
			true,
			Position(),
			sf::Sound( m_buffers[soundID] ) } );
		auto& sound = m_activeEffects.back().sound;
		sound.setPosition( 0, 0, 0 );
		sound.setRelativeToListener( true );
		rebalanceSound( m_activeEffects.back() );
		sound.play();

		if ( Global::debugSound )
		{
			qDebug() << "playing sound " << soundID;
		}
	}
	else
	{
		if ( Global::debugSound )
		{
			qDebug() << "Unknown sound " << soundID;
		}
	}
}

void AggregatorSound::rebalanceSound( ActiveEffect& effect )
{
	//TODO Check if we can get OpenAL band pass and reverb filters somehow working with SFML
	// Need them to properly do obstructed sound sources and "stone" environments
	effect.sound.setRelativeToListener( effect.isAbsolute );
	if ( !effect.isAbsolute )
	{
		if ( effect.pos.z > m_viewLevel )
		{
			effect.sound.setVolume( 0 );
		}
		else
		{
			//TODO Check for line-of-sight from camera, not just Z-cutoff
			constexpr int zCutoff = 20;
			auto diff             = std::min( m_viewLevel - effect.pos.z, zCutoff );
			effect.sound.setVolume( 100 - ( 100 * diff / zCutoff ) );
		}
	}
	else
	{
		effect.sound.setVolume( 100 );
	}
}

void AggregatorSound::garbageCollection()
{
	//TODO Get the slot fed externally!
	setVolume( Global::cfg->get( "AudioMasterVolume" ).toFloat() );

	for ( auto it = m_activeEffects.begin(); it != m_activeEffects.end(); )
	{
		if ( it->sound.getStatus() == sf::SoundSource::Stopped )
		{
			it = m_activeEffects.erase( it );
		}
		else
		{
			++it;
		}
	}
}

void AggregatorSound::setVolume( float newvol )
{
	sf::Listener::setGlobalVolume( newvol );
}

void AggregatorSound::onCameraPosition( float x, float y, float z, int r, float scale )
{
	// Cull sounds according to new view level
	m_viewLevel = z;
	garbageCollection();
	for ( auto& effect : m_activeEffects )
	{
		rebalanceSound( effect );
	}

	// Compute new listener position
	//TODO Untangle the calculations, there was no need for trigonometry
	sf::Listener::setUpVector( 0.f, 0.f, 1.f );
	float angle = 0;
	float x_rotated;
	float y_rotated;
	sf::Vector3f direction;
	switch ( r )
	{
		case 0:
		{
			angle     = ( ( -45. ) * 3.1415 ) / 180.;
			direction = { 1.f, 1.f, -1.f };
			break;
		}
		case 1:
		{
			angle     = ( ( -45. - 90 ) * 3.1415 ) / 180.;
			direction = { 1.f, -1.f, -1.f };
			break;
		}
		case 2:
		{
			angle     = ( ( -45. - 90 - 90 ) * 3.1415 ) / 180.;
			direction = { -1.f, -1.f, -1.f };
			break;
		}
		case 3:
		{
			angle     = ( ( -45. - 90 - 90 - 90 ) * 3.1415 ) / 180.;
			direction = { -1.f, 1.f, -1.f };
			break;
		}
	}
	sf::Listener::setDirection( direction );

	x = -x / 32 + Global::dimX / 2;
	y = -y / 16;
	x = x - Global::dimX / 2;
	y = y - Global::dimY / 2;
	x = x * 1.41421; // sqrt(2)
	y = y * 1.41421;

	float s = sin( angle );
	float c = cos( angle );

	x_rotated = x;
	y_rotated = y;

	float xnew = x_rotated * c - y_rotated * s;
	float ynew = x_rotated * s + y_rotated * c;

	x_rotated = xnew + Global::dimX / 2;
	y_rotated = ynew + Global::dimY / 2;

	// Center of view on the currently active top-most layer
	sf::Vector3f centerOfView = { x_rotated, y_rotated, z };
	// Virtual listener is actually floating above
	sf::Vector3f listener = centerOfView - direction * 100.f / scale;

	qDebug() << "changeViewPosition x " << listener.x << " y " << listener.y << " z " << listener.z;
	sf::Listener::setPosition( centerOfView - direction * 100.f / scale );
}
