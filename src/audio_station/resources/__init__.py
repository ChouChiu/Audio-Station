from importlib.resources import files


def resource_path(name: str):
    return files(__package__).joinpath(name)
